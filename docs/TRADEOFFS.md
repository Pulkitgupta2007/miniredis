# MiniRedis Design Decisions & Engineering Trade-Offs

Systems design is the discipline of navigating trade-offs. This document outlines the rationale behind key technical choices made in MiniRedis.

---

## 1. Storage Layout: Single Map vs. Separate Dictionaries

- **Option A (`std::unordered_map` wrapper)**:
  Encapsulates standard library separate chaining with bucket-level forward lists.
  - *Pros*: Battle-tested, zero bug surface, clean RAII memory safety, exception safety.
  - *Cons*: Node-based allocation introduces heap fragmentation and cache misses (pointer chasing through buckets). Stop-the-world rehashing can produce latency spikes when resizing large tables.
- **Option B (Custom Chained Hash Table - Stretch Goal implemented in `miniredis::ChainedHashTable`)**:
  Manual array of buckets with singly linked `Node` structs and power-of-two growth.
  - *Pros*: Full control over memory layout, predictable load factor triggers, direct interview explainability.
  - *Cons*: Still relies on node pointer dereferencing for collision chains.
- **Option C (Open-Addressing / Robin Hood / Swiss Table)**:
  Stores elements directly in contiguous slots without linked list nodes.
  - *Pros*: Exceptional CPU cache locality and zero node allocation overhead.
  - *Cons*: Deletions require tombstone markers or backward-shift deletion; high load factors degrade performance severely.
- **Option D (Redis Production `dict` with Progressive Rehashing)**:
  Redis maintains two tables (`ht[0]` and `ht[1]`). During rehash, each query/command migrates a fixed number of buckets (`dictRehash(d, 1)`) so client latency never spikes.

### Decision
For Phase 1, `Store` wraps `std::unordered_map` to maintain a robust, leak-free foundation while fully implementing Redis command semantics. We implement `ChainedHashTable` alongside it as an explainable systems milestone. In later phases, progressive rehashing can be plugged in without changing the `Store` public contract.

---

## 2. Expiration Data Structure: Min-Heap vs. Separate Map vs. Swap-and-Pop Vector

- **Option A (Min-Heap / Priority Queue)**:
  `std::priority_queue<pair<TimePoint, string>>` ordered by earliest expiration.
  - *Pros*: Finding the earliest expiring key is $\mathcal{O}(1)$.
  - *Cons*: **Cannot support random sampling**. Removing a specific key on `DEL` or `PERSIST`, or updating TTL on `EXPIRE`, requires either $\mathcal{O}(N)$ linear scan through the heap or maintaining heap node indices with sift-up/sift-down operations ($\mathcal{O}(\log N)$). Furthermore, an attacker creating keys that expire in the distant future can push useless elements that block top-of-heap checks.
- **Option B (Separate Expiry Map with Bucket Probing like Redis C)**:
  `std::unordered_map<string, TimePoint>` where random buckets are inspected.
  - *Pros*: $\mathcal{O}(1)$ lookup, update, and deletion.
  - *Cons*: In C++, probing random buckets of `std::unordered_map` encounters empty buckets when load factor is low, resulting in variable sampling latency and CPU waste.
- **Option C (Contiguous Array + Reverse Index Map - MiniRedis Implementation)**:
  Combines `std::unordered_map<string, TimePoint> expires_` with `std::vector<string> expiry_keys_` and `std::unordered_map<string, size_t> expiry_index_`.
  - *Pros*:
    - **True $\mathcal{O}(1)$ Uniform Random Sampling**: Generating a random index in $[0, \text{size}-1]$ directly accesses an expiring key in $\mathcal{O}(1)$ without empty-bucket misses.
    - **$\mathcal{O}(1)$ Deletion**: Swap-and-pop (swapping the target element with the back of the vector, then calling `pop_back()`) achieves constant-time deletion.
    - **Cache Locality**: Expiry keys are packed contiguously in memory.
  - *Cons*: Consumes an additional hash map entry (`expiry_index_`) per expiring key.

### Decision
MiniRedis implements Option C. It provides strictly bounded, predictable sampling latency for the active expiration cycle while keeping all TTL operations (`SET EX`, `EXPIRE`, `PERSIST`, `DEL`) strictly $\mathcal{O}(1)$.

---

## 3. Clock Selection: `std::chrono::steady_clock` vs `system_clock`

- **`system_clock`**: Tracks wall-clock time. Vulnerable to NTP adjustments, step corrections, and leap seconds, which can cause keys to prematurely expire or live indefinitely.
- **`steady_clock`**: Monotonic clock with guarantees that `now()` never moves backwards. Ideal for measuring elapsed durations and timeouts.

### Decision
MiniRedis exclusively uses `steady_clock`. To support serialization/persistence across process restarts in later phases, relative TTL offsets are stored rather than absolute machine timestamps.

---

## 4. Test Determinism: Sleep vs Mock Clock Injection

- **Sleep-based Testing**: Tests call `std::this_thread::sleep_for(100ms)`.
  - Flaky under heavy CI load or CPU throttling.
  - Artificially slows down test suites.
- **Clock Injection**: `Store` accepts a lambda `ClockFn` that returns `TimePoint`.
  - Tests step simulated time forward by any arbitrary duration instantaneously ($0\text{ ms}$).
  - Guarantees 100% test reproducibility across all platforms.
