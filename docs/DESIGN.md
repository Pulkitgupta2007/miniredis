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

### 2. Expiration Strategy (Dual Model)
Redis employs a hybrid strategy to balance CPU cycles and memory usage:

1. **Passive (Lazy) Expiration**:
   - Whenever any command (`GET`, `EXISTS`, `DEL`, `TTL`) touches a key, `check_and_expire(key)` is invoked.
   - If the current monotonic timestamp exceeds `expires_at`, the entry is excised from `data_` and `expires_`, returning non-existent semantics to the caller.
   - *Cost*: Zero background CPU; memory remains occupied until the key is accessed.

2. **Active (Probabilistic) Expiration**:
   - A periodic task (`active_expire_cycle`) runs at fixed intervals (e.g. 10 Hz in the event loop).
   - Up to $N$ keys (default 20) are sampled from `expires_`.
   - Expired keys in the sample are immediately purged.
   - If the proportion of expired keys in the sample exceeds **25%**, the cycle repeats immediately up to a bounded iteration ceiling to avoid starving client requests.
   - *Cost*: Controlled, bounded CPU budget; bounds unaccessed memory leakage.

### 3. Monotonic Clocks and Test Determinism
- Wall-clock time (`system_clock`) is susceptible to NTP sync adjustments, leap seconds, and daylight saving shifts.
- MiniRedis uses `std::chrono::steady_clock` internally to ensure strictly monotonic intervals.
- The `Store` constructor accepts an injectable `ClockFn`, decoupling time from system state and enabling instant, 100% deterministic unit tests without sleep overhead.
