# MiniRedis System Design

MiniRedis is an educational yet production-grade Redis clone written in modern C++17, designed to demonstrate core systems programming principles: non-blocking I/O event loops, the RESP2 wire protocol, in-memory data structures, cache expiration algorithms, and full-stack observability.

---

## Architecture Overview

MiniRedis is decomposed into modular layers:

```
+-----------------------------------------------------------+
|                     Clients (redis-cli)                   |
+-----------------------------------------------------------+
                              |
                              v
+-----------------------------------------------------------+
|             Networking & Event Loop (epoll / kqueue)      |
|  - Non-blocking TCP socket multiplexer                    |
|  - Connection state machine & per-client buffer mgmt      |
+-----------------------------------------------------------+
                              |
                              v
+-----------------------------------------------------------+
|               RESP2 Protocol Parser / Serializer          |
|  - Streaming zero-copy parser for RESP frames             |
+-----------------------------------------------------------+
                              |
                              v
+-----------------------------------------------------------+
|                 Command Dispatcher & Router               |
|  - Validates arity, executes command, encodes reply       |
+-----------------------------------------------------------+
                              |
                              v
+-----------------------------------------------------------+
|                  Storage Engine (Store)                   |
|  - Primary hash table (data_)                             |
|  - Auxiliary TTL hash set (expires_)                      |
|  - Hybrid Passive & Active TTL Eviction                   |
+-----------------------------------------------------------+
```

---

## Phase 1: Storage Engine & Key-Value Core

### 1. Data Structures
- **Primary Data Store (`data_`)**: `std::unordered_map<std::string, Entry>`
  - Why wrap `std::unordered_map`?
    - Provides a standard, collision-resistant separate chaining hash table with $\mathcal{O}(1)$ average complexity.
    - Clean separation of concerns: table memory management and bucket distribution are isolated from Redis-specific semantics (NX/XX condition flags, monotonic TTL expiration, binary safety).
  - `Entry` contains:
    - `value`: `std::string`
    - `expires_at`: `std::optional<std::chrono::steady_clock::time_point>`
- **TTL Index (`expires_`)**: `std::unordered_set<std::string>`
  - Tracks keys that have an associated TTL.
  - Avoids full database scans during periodic active expiration sweeps.
- **Custom Chained Hash Table (`ChainedHashTable<K, V>`) [Stretch Goal]**:
  - Located in [`server/include/miniredis/chained_hash_table.h`](file:///Users/pulkit/miniredis/server/include/miniredis/chained_hash_table.h).
  - Demonstrates manual implementation of a bucket array with singly linked nodes, pointer manipulation, and dynamic power-of-two rehashing triggered at load factor $\ge 1.0$.

### 2. Binary Safety
In C, strings are null-terminated (`\0`), making it impossible to store binary data (such as protobufs, encrypted tokens, or compressed buffers) containing embedded null bytes without truncation. MiniRedis keys and values use `std::string` and `std::string_view`, which explicitly store `size` alongside a continuous buffer pointer. This guarantees full binary safety across all commands (`SET`, `GET`, `DEL`, `KEYS`).

### 3. Expiration Strategy (Dual Model)
Redis employs a hybrid strategy to balance CPU cycles and memory usage:

1. **Lazy (Passive) Expiration on Access**:
   - Whenever any command (`GET`, `EXISTS`, `DEL`, `TTL`, `KEYS`) touches a key, `check_and_expire(key)` is invoked.
   - If `current_time() >= expires_at`, the entry is excised from `data_` and `remove_expiry(key)` is called, returning non-existent semantics to the caller.
   - *Cost*: Zero background CPU; memory remains occupied until the key is accessed.

2. **Active (Probabilistic) Expiration with Time Budget**:
   - Runs periodically during event loop idle ticks (e.g. 10 Hz).
   - Rather than scanning all keys ($\mathcal{O}(N)$), it SAMPLES random keys from `expiry_keys_`.
   - **Data Structure for $\mathcal{O}(1)$ Sampling**:
     - `expires_`: `std::unordered_map<std::string, TimePoint>` for timestamp lookup.
     - `expiry_keys_`: `std::vector<std::string>` storing only keys with active TTLs contiguously in memory.
     - `expiry_index_`: `std::unordered_map<std::string, size_t>` storing each key's index in `expiry_keys_`.
     - *Insertion*: append to vector and record index ($\mathcal{O}(1)$).
     - *Deletion*: swap with the last element in the vector, update index, and pop back ($\mathcal{O}(1)$ swap-and-pop idiom).
     - *Random Sampling*: sample $K$ random indices from $[0, \text{size}-1]$ without replacement in $\mathcal{O}(K)$ time.
   - **Adaptive 25% Loop & Time Budget**:
     - Samples $K$ keys (default 20).
     - Purges expired keys in the sample.
     - If the proportion of expired keys in the sample exceeds **25%**, the cycle repeats immediately to accelerate memory recovery.
     - If elapsed execution time exceeds `time_budget` (default 25ms), the cycle halts immediately (`timelimit_reached = true`) to prevent event loop latency spikes.

### 4. Monotonic Clocks and Zero-Sleep Deterministic Testing
- Wall-clock time (`system_clock`) is susceptible to NTP sync adjustments, leap seconds, and daylight saving shifts.
- MiniRedis uses `std::chrono::steady_clock` internally to ensure strictly monotonic intervals.
- The `Store` constructor accepts an injectable `ClockFn`, decoupling time from system state and enabling instant, 100% deterministic unit tests without `sleep()` overhead.
