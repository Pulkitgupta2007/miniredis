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

### 5. Memory Accounting & Eviction Engine (LRU and LFU)

MiniRedis implements strict memory accounting and pluggable eviction policies configurable via `CONFIG SET maxmemory` and `CONFIG SET maxmemory-policy`:

```
+-----------------------------------------------------------+
|               Write Command Execution (SET)               |
+-----------------------------------------------------------+
                              |
                     Estimate Memory Delta
                              |
                              v
        +-------------------------------------------+
        |  used_memory + delta > maxmemory ?        |
        +-------------------------------------------+
               |                             |
              YES                            NO
               |                             |
      Check Policy                           v
        /        \                   Commit Write to Store
       /          \                  Update LRU & LFU Trackers
      v            v
[noeviction]   [allkeys-lru / allkeys-lfu]
      |                    |
Return -OOM         Eviction Loop:
                    - Pop victim key in O(1)
                    - Free memory
                    - Record to EvictionRingBuffer
```

#### A. Custom $\mathcal{O}(1)$ LRU Tracker
- **Data Structures**: `std::unordered_map<std::string, Node*>` paired with a doubly linked list with sentinel `head` (MRU) and `tail` (LRU) nodes.
- **Operations**:
  - `add(key)`: Inserts at head in $\mathcal{O}(1)$.
  - `touch(key)`: Splices node from its current position to the head in $\mathcal{O}(1)$.
  - `pop_lru()`: Detaches `tail->prev` in $\mathcal{O}(1)$.

#### B. Custom $\mathcal{O}(1)$ LFU Tracker
- **Data Structures**:
  - `key_map_`: `std::unordered_map<std::string, Node*>` tracking node references.
  - `freq_buckets_`: `std::unordered_map<size_t, FreqBucket*>` mapping access count $\to$ Doubly Linked List of keys.
  - `min_freq_`: Tracks current global minimum frequency.
- **Operations**:
  - `add(key)`: Inserts key with frequency 1 at head of `freq_buckets_[1]`; sets `min_freq_ = 1` ($\mathcal{O}(1)$).
  - `touch(key)`: Detaches node from `freq_buckets_[old_freq]`; increments `min_freq_` if old bucket is now empty; attaches to head of `freq_buckets_[old_freq + 1]` ($\mathcal{O}(1)$).
  - `pop_lfu()`: Pops from `tail` of `freq_buckets_[min_freq_]` ($\mathcal{O}(1)$).
  - **Tie-Breaking**: Within the same frequency bucket, keys are arranged in MRU-to-LRU order. Evicting from `tail` guarantees that the least recently accessed key among those with the minimum frequency is chosen.

#### C. In-Memory Eviction Ring Buffer
- Fixed-capacity circular array of `EvictionEvent` structs (`key`, `policy`, `timestamp_ms`, `freed_bytes`).
- Non-allocating after initialization; stores the last $N$ evictions for real-time telemetry streaming to the Node.js bridge and React dashboard.

---

## 6. Persistence Engine (Append-Only File - AOF)

MiniRedis provides crash recovery and durability via an Append-Only File (AOF) logger executing at the server event loop layer:

```
+-----------------------------------------------------------+
|               Write Command Execution (SET, etc)          |
+-----------------------------------------------------------+
                              |
                     Command Succeeded
                              |
                              v
        +-------------------------------------------+
        |  AOF Logger: Serialize to RESP Array      |
        +-------------------------------------------+
                              |
                    write(aof_fd, frame)
                              |
                              v
        +-------------------------------------------+
        | Fsync Policy Check:                       |
        | - ALWAYS:   fsync() immediately           |
        | - EVERYSEC: background thread syncs 1 Hz  |
        | - NO:       rely on OS page cache flushes |
        +-------------------------------------------+
```

### 1. Absolute Timestamp Translation for TTL
Relative expirations (`SET k v EX 10`) cannot be logged directly; replaying them upon server reboot would erroneously extend key lifetimes by another 10 seconds.
- During logging and rewrite, relative timeouts are translated to absolute Unix timestamps:
  `PEXPIREAT <key> <absolute_epoch_ms>`
- On replay, `pexpireat(key, absolute_ms)` calculates remaining time relative to current epoch time.
- If the timestamp is in the past (`remaining <= 0`), the key is discarded immediately and **never resurrected**.

### 2. Crash Recovery & Partial Record Truncation
Power failures or unexpected crashes during a write operation can leave a half-written RESP frame at the tail of the AOF log.
- On startup, `AofManager::replay()` parses frames iteratively while recording the byte offset of the last valid command.
- If the stream ends with an `Incomplete` or corrupted frame, MiniRedis detects the incomplete record, issues a warning, and calls `ftruncate(fd, last_valid_offset)`.
- Normal startup resumes with zero manual operator intervention.

### 3. Atomic Log Compaction (`BGREWRITEAOF`)
As the dataset mutates, the AOF log accumulates obsolete commands (e.g. intermediate updates to a counter or deleted keys).
- The rewrite process snapshots all live keys in the current `Store`.
- Compiles the minimal state into a temporary file: `<filename>.rewrite.tmp`.
- Calls `fsync()` to ensure complete media flush.
- Atomically replaces the active log using the POSIX atomic primitive `rename(tmp, target)`.
- Guarantees crash safety: the active AOF log is never in a half-written or corrupted state.

---

## 7. Observability & Telemetry Subsystem

MiniRedis provides first-class runtime visibility into its health, throughput, memory efficiency, and eviction activity:

```
+-----------------------------------------------------------+
|                      Client Request                       |
+-----------------------------------------------------------+
                              |
                     Parse & Route Command
                              |
                              v
         +-------------------------------------------+
         |  Metrics Collection:                      |
         |  - total_commands_processed++             |
         |  - keyspace_hits / keyspace_misses        |
         |  - expired_keys / evicted_keys            |
         +-------------------------------------------+
                              |
               +--------------+--------------+
               |                             |
               v                             v
      [INFO Command]                  [EVENTS Command]
  Standard Redis-formatted         In-memory ring buffer query
  key:value telemetric stats:      returning JSON-formatted eviction
  - Memory & key count             and expiration event stream
  - Hit ratio & ops/sec            for Node.js dashboard bridge
  - Uptime & connected clients
```

### 1. Zero-Cost Metrics Tracking
- Monotonic counters (`total_commands_processed`, `keyspace_hits`, `keyspace_misses`, `expired_keys`, `evicted_keys`) are updated inline with $\mathcal{O}(1)$ operations without memory allocation.
- `hit_ratio` is calculated on demand as $\frac{\text{hits}}{\text{hits} + \text{misses}} \times 100.0$.
- `ops_per_sec` tracks real-time instantaneous throughput by measuring command throughput across sliding 1-second epochs.

### 2. Standard `INFO` Command
Formatted as newline-delimited `field:value\r\n` lines organized into logical sections (`# Server`, `# Clients`, `# Memory`, `# Persistence`, `# Stats`):
- `used_memory`: Bytes allocated by keys, values, and tracker structures.
- `total_keys`: Count of currently active keys.
- `keyspace_hits` & `keyspace_misses`: Cache effectiveness counters.
- `hit_ratio`: Cache hit percentage (`0.00%` - `100.00%`).
- `evicted_keys`: Total keys evicted due to `maxmemory` constraints.
- `expired_keys`: Total keys purged via passive or active TTL expiration.
- `connected_clients`: Active client TCP connections.
- `total_commands_processed`: Lifetime commands serviced.
- `ops_per_sec`: Instantaneous operations executed per second.
- `uptime_in_seconds`: Server elapsed runtime.

### 3. Telemetry Event Stream (`EVENTS` / `MONITOR`)
- A fixed-capacity, non-blocking `TelemetryRingBuffer` records recent system events (`EVICTION`, `EXPIRATION`).
- The `EVENTS [count]` command exports recent history formatted for ingestion by the upcoming Node.js WebSocket bridge and React visualization dashboard.

