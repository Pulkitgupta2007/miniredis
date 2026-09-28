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

## 2. Expiration: Per-Key Timers vs. Hybrid Passive/Active Sweeps

- **Option A (Timer per Key / Min-Heap Priority Queue)**:
  Maintain a min-heap sorted by expiration timestamp, or register OS timers (`timerfd`) per key.
  - *Trade-off*: Insertion and deletion cost climbs to $O(\log N)$. Modifying TTLs requires an expensive heap sift-up/down or index mapping. Hundreds of thousands of simultaneous timers consume significant memory and CPU overhead.
- **Option B (Redis-Style Hybrid Passive + Active Sampling)**:
  Passive eviction on access + periodic probabilistic sampling from keys with TTL.
  - *Trade-off*: Expired keys that are never read again linger in memory until picked up by active sampling. However, average CPU cost remains $O(1)$ per operation, and memory consumption remains strictly bounded.

### Decision
Option B matches Redis production semantics. It preserves constant-time $O(1)$ SET/GET performance.

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
