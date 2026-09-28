# MiniRedis Design Decisions & Engineering Trade-Offs

Systems design is the discipline of navigating trade-offs. This document outlines the rationale behind key technical choices made in MiniRedis.

---

## 1. Storage Layout: Single Map vs. Separate Dictionaries

- **Option A (Standard Library Map with Inlined Metadata)**:
  `unordered_map<string, Entry>` where `Entry = { value, expires_at }`, supplemented by `unordered_set<string> expires_`.
- **Option B (Dual Dictionary like Redis C Implementation)**:
  `dict data` and `dict expires` where `expires` maps `key -> timestamp`.

### Decision
Option A was chosen for clean C++ encapsulation and cache locality. Storing `expires_at` alongside `value` eliminates a secondary hash lookup on reads (`GET` only probes `data_`). The auxiliary `expires_` set tracks keys with active TTLs so the background `active_expire_cycle()` does not iterate over millions of non-expiring keys.

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
