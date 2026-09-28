# MiniRedis System Design & Architectural Blueprint

MiniRedis is an educational yet production-grade Redis clone written in modern C++17, designed to demonstrate core systems programming principles: non-blocking I/O event loops, the RESP2 wire protocol, in-memory data structures, cache expiration algorithms, durability through append-only logging, and full-stack observability.

---

## 1. Architecture Overview

MiniRedis is decomposed into modular layers running on a single-threaded reactor event loop with background worker threads reserved exclusively for disk durability synchronization (`fsync`):

```
+-----------------------------------------------------------------------------------+
|               Clients (redis-cli / redis-benchmark / Node.js Bridge)              |
+-----------------------------------------------------------------------------------+
                                          |
                                          v  [Raw TCP Sockets - Non-Blocking]
+-----------------------------------------------------------------------------------+
|               Networking & Reactor Layer (Server / Poller)                        |
|  - epoll (Linux) / kqueue (macOS) multiplexing                                    |
|  - Edge-triggered non-blocking I/O with per-client buffer management              |
|  - Signal handling via Unix Self-Pipe trick (SIGINT / SIGTERM)                    |
+-----------------------------------------------------------------------------------+
                                          |
                                          v  [Byte Streams]
+-----------------------------------------------------------------------------------+
|               RESP2 Protocol Parser & Serializer (RespParser)                     |
|  - Streaming zero-copy tokenization (std::string_view)                            |
|  - RESP Arrays (*), Bulk Strings ($), Integers (:), Errors (-), Simple (+)        |
|  - Inline command parsing fallback                                                |
|  - Security bounds (512MB bulk limit, 1M array limit, 64 recursion limit)         |
+-----------------------------------------------------------------------------------+
                                          |
                                          v  [Command Tokens]
+-----------------------------------------------------------------------------------+
|               Command Dispatcher & Router (Server::process_client_input)          |
|  - Validates command arity, executes logic, produces RESP replies                 |
|  - Tracks keyspace metrics (hits, misses, commands, ops/sec)                      |
+-----------------------------------------------------------------------------------+
                  |                               |                     |
                  v                               v                     v
+-------------------------------+ +-----------------------------+ +--------------------+
|    Storage Engine (Store)     | |  Eviction Engine (LRU/LFU)  | |  AOF Persistence   |
| - std::unordered_map /        | | - CONFIG SET maxmemory      | | - append-only log  |
|   ChainedHashTable            | | - Exact O(1) LRU (DLL + Map)| | - fsync policies   |
| - Binary-safe std::string     | | - Exact O(1) LFU (Buckets)  | | - PEXPIREAT epoch  |
| - Monotonic steady_clock TTL  | | - TelemetryRingBuffer       | | - Auto truncation  |
| - O(1) Active Expiry Sampling | | - OOM rejection on writes   | | - Atomic rename()  |
+-------------------------------+ +-----------------------------+ +--------------------+
```

---

## 2. Comprehensive Data Structures & Complexity Matrix

The following matrix documents every core subsystem, its internal data structures, and its asymptotic time and space complexities:

| Subsystem | Component | Data Structure | Operation | Average Time | Worst Time | Space Overhead |
|---|---|---|---|---|---|---|
| **Storage Engine** | Primary Key-Value Store | `std::unordered_map<std::string, Entry>` | `GET` / `EXISTS` | $\mathcal{O}(1)$ | $\mathcal{O}(N)$ (hash collision) | $\sim 32\text{B} + \text{key} + \text{val}$ |
| | | | `SET` / `DEL` | $\mathcal{O}(1)$ | $\mathcal{O}(N)$ (hash collision) | $\sim 32\text{B} + \text{key} + \text{val}$ |
| | | | `KEYS <pattern>` | $\mathcal{O}(N)$ | $\mathcal{O}(N)$ | $\mathcal{O}(K)$ matches |
| | Custom Chained Table | `ChainedHashTable<K, V>` | `insert` / `find` / `erase` | $\mathcal{O}(1)$ | $\mathcal{O}(N)$ | Bucket array + nodes |
| **Expiration System** | Expiry Lookup Map | `std::unordered_map<std::string, TimePoint>` | Lookup / Update | $\mathcal{O}(1)$ | $\mathcal{O}(N)$ | $\sim 48\text{B}$ per expiring key |
| | Expiry Contiguous Keys | `std::vector<std::string>` | Random Sampling ($K$) | $\mathcal{O}(K)$ | $\mathcal{O}(K)$ | Contiguous strings |
| | Expiry Reverse Index | `std::unordered_map<std::string, size_t>` | Swap-and-Pop Delete | $\mathcal{O}(1)$ | $\mathcal{O}(N)$ | $\sim 40\text{B}$ per expiring key |
| | Passive Expiry | Inline access check | `check_and_expire` | $\mathcal{O}(1)$ | $\mathcal{O}(1)$ | None |
| | Active Expiry Loop | Probabilistic sampler | Sweep 20 keys ($\le 25\text{ms}$) | $\mathcal{O}(K)$ | $\mathcal{O}(K)$ (clamped) | None |
| **Eviction Engine** | LRU Tracker | Sentinel Doubly-Linked List + Hash Map | `touch` / `add` | $\mathcal{O}(1)$ | $\mathcal{O}(1)$ | 2 pointers + node per key |
| | | | `pop_lru` (Evict) | $\mathcal{O}(1)$ | $\mathcal{O}(1)$ | None |
| | LFU Tracker | Map of Frequency Buckets (`size_t` $\to$ DLL) + Map | `touch` / `add` | $\mathcal{O}(1)$ | $\mathcal{O}(1)$ | Frequency buckets + nodes |
| | | | `pop_lfu` (Evict) | $\mathcal{O}(1)$ | $\mathcal{O}(1)$ | None |
| | Memory Tracking | Monotonic `used_memory_` byte counter | `update_memory` | $\mathcal{O}(1)$ | $\mathcal{O}(1)$ | 8 bytes |
| | Eviction Telemetry | Fixed-capacity `TelemetryRingBuffer` | `push` / `get_recent` | $\mathcal{O}(1)$ / $\mathcal{O}(M)$ | $\mathcal{O}(1)$ / $\mathcal{O}(M)$ | $M \times \text{EventSize}$ |
| **Networking** | Reactor Multiplexer | `Poller` (`epoll` on Linux, `kqueue` on macOS) | Register / Deregister | $\mathcal{O}(1)$ / $\mathcal{O}(\log N)$ | $\mathcal{O}(\log N)$ | Kernel interest list |
| | | | Event Wait (`poll`) | $\mathcal{O}(\text{ready})$ | $\mathcal{O}(\text{ready})$ | Event buffer |
| | Connection Manager | `std::unordered_map<int, Client>` | Socket lookup / cleanup | $\mathcal{O}(1)$ | $\mathcal{O}(N)$ | Per-client buffer |
| | Graceful Teardown | Unix Self-Pipe (`pipe[2]`) | Signal notification | $\mathcal{O}(1)$ | $\mathcal{O}(1)$ | 2 file descriptors |
| **Protocol Parser** | RESP2 Parser | Streaming `RespParser` (`std::string_view`) | `parse` frame | $\mathcal{O}(L)$ ($L = \text{bytes}$) | $\mathcal{O}(L)$ | Zero allocations |
| **Persistence (AOF)** | Write-Ahead Logger | `AofManager` (File Descriptor `write()`) | Append write frame | $\mathcal{O}(L)$ | $\mathcal{O}(L)$ | OS Page Cache |
| | Background Fsync | Worker Thread (`std::thread` + conditional) | `fsync()` at 1 Hz | $\mathcal{O}(\text{dirty})$ | Disk I/O bound | 1 thread stack |
| | Atomic Compaction | Temp file + POSIX `rename()` | `rewrite_aof` | $\mathcal{O}(N)$ keys | $\mathcal{O}(N)$ keys | Proportional to DB size |

---

## 3. Component Details & Implementation Architecture

### Phase 1: Storage Engine & Key-Value Core

#### 1. Primary Data Store (`data_`)
- Encapsulates `std::unordered_map<std::string, Entry>`.
- `Entry` contains:
  - `value`: `std::string` (binary-safe, capacity-tracked)
  - `expires_at`: `std::optional<std::chrono::steady_clock::time_point>`
- **Why wrap `std::unordered_map`?**
  - Isolates table allocation and collision chaining from Redis command logic (NX/XX flags, monotonic TTL, memory accounting).
  - Employs standard RAII resource cleanup with zero memory leak hazards.
- **Custom Chained Hash Table (`ChainedHashTable<K, V>`)**:
  - Implemented in [`server/include/miniredis/chained_hash_table.h`](file:///Users/pulkit/miniredis/server/include/miniredis/chained_hash_table.h) as an educational stretch goal.
  - Features manual bucket pointer arrays, singly linked node chains, and dynamic power-of-two growth triggered when load factor $\ge 1.0$.

#### 2. Binary Safety
In standard C, strings are null-terminated (`\0`), truncating binary data (protobufs, encryption keys, compressed blobs). MiniRedis keys and values use `std::string` and `std::string_view` which explicitly store `size` alongside buffer pointers, guaranteeing zero data corruption across embedded null bytes.

---

### Phase 2: Networking & Event Loop (epoll / kqueue Reactor)

MiniRedis implements a single-threaded non-blocking reactor multiplexing all I/O through a unified cross-platform abstraction:

```
+-----------------------------------------------------------+
|                   Main Reactor Event Loop                 |
+-----------------------------------------------------------+
                             |
                     poller.poll(timeout)
                             |
         +-------------------+-------------------+
         |                                       |
    [Listen FD]                             [Client FD]
         |                                       |
   accept() loop                           read() loop
   set_nonblocking()                       append to in_buf
   register with Poller                    process_client_input()
```

#### 1. Cross-Platform Poller Abstraction
- **Linux**: Employs `epoll_create1(0)`, `epoll_ctl()`, and `epoll_wait()`. Uses level-triggered readiness with non-blocking reads.
- **macOS / BSD**: Employs `kqueue()`, `EV_SET()`, and `kevent()`. Translates filter read events cleanly.
- Unified interface [`server/include/miniredis/poller.h`](file:///Users/pulkit/miniredis/server/include/miniredis/poller.h) shields the server engine from operating system divergences.

#### 2. Unix Self-Pipe Trick for Graceful Shutdown
Signal handlers cannot safely invoke non-reentrant functions (e.g. `malloc`, C++ standard library allocations, socket closures). MiniRedis registers a POSIX signal handler for `SIGINT` and `SIGTERM` that writes a single byte to `signal_pipe_[1]`. The read end `signal_pipe_[0]` is registered directly with the `Poller`. Upon receiving a signal, the reactor wakes synchronously, cleanly breaks its loop, and executes structured teardown on the main thread.

---

### Phase 3: TTL / Expiration Subsystem

MiniRedis uses a dual passive + active expiration model:

```
+--------------------------------------------------------------------+
|                         Expiration Engine                          |
+--------------------------------------------------------------------+
              |                                        |
              v                                        v
     [Passive Expiry]                         [Active Expiry]
     - Triggered on command access             - Periodic cycle (10 Hz)
     - check_and_expire(key)                   - Samples 20 random keys in O(1)
     - Purges entry if now >= expires_at       - If >25% expired, repeats
                                               - Bounded by 25ms time budget
```

#### 1. True $\mathcal{O}(1)$ Random Sampling Data Structure
A standard hash table cannot be sampled uniformly at random in $\mathcal{O}(1)$ time because empty buckets at low load factors cause sampling misses. MiniRedis solves this using three synchronized structures:
1. `expires_`: `std::unordered_map<std::string, TimePoint>` for $\mathcal{O}(1)$ timestamp lookup.
2. `expiry_keys_`: `std::vector<std::string>` storing only expiring keys contiguously.
3. `expiry_index_`: `std::unordered_map<std::string, size_t>` storing each key's array index.

**Swap-and-Pop Deletion**:
When an expiring key is deleted or refreshed:
```cpp
size_t idx = expiry_index_[key];
std::string last_key = expiry_keys_.back();
expiry_keys_[idx] = last_key;
expiry_index_[last_key] = idx;
expiry_keys_.pop_back();
expiry_index_.erase(key);
expires_.erase(key);
```
This guarantees **true $\mathcal{O}(1)$ insertion, update, deletion, and random sampling**.

#### 2. Adaptive Active Expiry Cycle
- Samples 20 keys uniformly at random from `expiry_keys_`.
- Deletes expired keys.
- If $>25\%$ of the sample was expired, the loop repeats immediately to clear memory pressure.
- Halts immediately when execution time reaches `kActiveExpireBudgetMs = 25ms` to prevent event-loop latency spikes.

---

### Phase 4: Eviction Engine (LRU and LFU)

MiniRedis implements configurable memory limits via `CONFIG SET maxmemory <bytes>` and `CONFIG SET maxmemory-policy [noeviction | allkeys-lru | allkeys-lfu]`.

#### 1. Memory Accounting
Memory footprint per key-value entry is calculated deterministically:
$$\text{Entry Memory} = \text{key.size}() + \text{value.size}() + \text{sizeof(Entry)} + \text{kTrackerOverhead}$$
Where `kTrackerOverhead = 64` bytes accounts for hash bucket nodes and tracker pointers.

#### 2. Custom Exact $\mathcal{O}(1)$ LRU Cache
- Implemented in [`server/include/miniredis/lru_tracker.h`](file:///Users/pulkit/miniredis/server/include/miniredis/lru_tracker.h).
- Uses a hash map `std::unordered_map<std::string, Node*>` paired with an explicit doubly linked list with sentinel `head` (MRU) and `tail` (LRU) nodes.
- `touch(key)`: Detaches node and splices to head in $\mathcal{O}(1)$.
- `pop_lru()`: Detaches `tail->prev` in $\mathcal{O}(1)$.

#### 3. Custom Exact $\mathcal{O}(1)$ LFU Cache (Frequency Buckets)
- Implemented in [`server/include/miniredis/lfu_tracker.h`](file:///Users/pulkit/miniredis/server/include/miniredis/lfu_tracker.h).
- Structure:
  - `key_map_`: `std::unordered_map<std::string, Node*>`
  - `freq_buckets_`: `std::unordered_map<size_t, FreqBucket*>` mapping frequency $\to$ Doubly Linked List of keys.
  - `min_freq_`: Tracks current global minimum frequency.
- **LRU Tie-Breaking**: Keys inside each frequency bucket are maintained in MRU-to-LRU order. When evicting, the node at the tail of `freq_buckets_[min_freq_]` is selected, guaranteeing that the least-recently used key among lowest-frequency candidates is evicted.

---

### Phase 5: Append-Only File (AOF) Persistence & Crash Recovery

MiniRedis logs all state-mutating commands to disk in RESP format:

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

#### 1. Relative to Absolute Epoch Translation
Relative TTL options (`SET k v EX 10`) cannot be logged directly, as replaying them on server reboot would resurrect expired keys. MiniRedis translates all relative expirations to absolute epoch millisecond timestamps:
$$\text{PEXPIREAT} \quad \text{key} \quad \text{epoch\_ms}$$
On replay, `pexpireat()` calculates remaining time relative to current wall-clock time. If the timestamp has passed, the key is pruned immediately.

#### 2. Crash Truncation Auto-Recovery
A power loss or crash mid-write leaves a partial RESP frame at the tail of the log. During startup replay, `AofManager` tracks the byte offset of the last complete command. Upon encountering an incomplete or corrupted frame at EOF, it logs a warning and calls:
```cpp
::ftruncate(fd, last_valid_offset);
```
Truncating the invalid tail allows the server to recover automatically without manual intervention.

#### 3. Atomic Compaction (`BGREWRITEAOF`)
Compaction serializes current memory state into a temporary file (`<path>.rewrite.tmp`), calls `fsync()` to flush physical disk buffers, and atomically swaps the file using `::rename()`.

---

### Phase 6: Observability & Telemetry Subsystem

- **`INFO` Command**: Returns standard Redis-formatted key-value telemetry organized into `# Server`, `# Clients`, `# Memory`, `# Persistence`, and `# Stats`.
- **Sliding-Window Throughput Tracking**: Instantaneous `ops_per_sec` is tracked using 1-second sliding time windows.
- **`EVENTS [limit]` Command**: Streams real-time eviction and expiration events from a non-blocking in-memory circular ring buffer (`TelemetryRingBuffer`).

---

### Phase 7: Node.js Bridge & Real-Time React Dashboard

```
+-----------------------------------------------------------+
|                   MiniRedis C++ Server                    |
|      (TCP Port 6379, RESP2 Protocol, epoll/kqueue)        |
+-----------------------------------------------------------+
                              ^
                              | Raw TCP Socket
                              v
+-----------------------------------------------------------+
|                    Node.js Bridge                         |
|  - Custom non-blocking TCP RESP client                    |
|  - Polling loop: INFO, LRUKEYS, EVENTS (1 Hz)             |
|  - WebSocket Server (Port 4000)                           |
|  - Bidirectional command proxy + offline state broadcast  |
+-----------------------------------------------------------+
                              ^
                              | WebSocket Protocol
                              v
+-----------------------------------------------------------+
|                   React Live Dashboard                    |
|  - Real-time SVG Memory & Ops/sec timeline graph          |
|  - Interactive MRU -> LRU Doubly Linked List Visualizer   |
|  - Eviction & Expiration activity feed                    |
|  - Interactive Web Command Console (SET/GET/DEL/TTL)      |
|  - Reconnection state banner & server offline detection   |
+-----------------------------------------------------------+
```

1. **Zero-Dependency Node.js Bridge**: Built using native `net.Socket`. Maintains an in-flight promise queue matching Redis's single-threaded command sequence.
2. **Dynamic Mutation Invalidation**: When commands are executed in the web console, an immediate telemetry poll is dispatched to provide sub-10ms UI updates.
3. **Pure SVG React Charts**: Real-time vector rendering of memory usage and operations per second without heavy charting library bloat.
4. **Interactive LRU Visualizer**: Visual representation of the internal doubly linked list from MRU head to LRU tail with live updates.

---

### Phase 8: Concurrency, Fuzzing & Security Hardening

To guard against adversarial network streams, the RESP parser and network buffers enforce strict boundaries:
- **Bulk String Cap**: Maximum 512 MB (`kMaxBulkStringLength = 512 * 1024 * 1024`). Reject negative lengths ($< -1$).
- **Array Size Cap**: Maximum 1,000,000 elements (`kMaxArrayElements = 1024 * 1024`). Pre-allocation is clamped to `std::min(count, 1024)` to prevent speculative heap exhaustion.
- **Recursion Ceiling**: Maximum 64 levels (`kMaxRecursionDepth = 64`) to protect against stack-overflow attacks.
- **Client Buffer Ceiling**: Per-client buffer capped at 64 MB. Incomplete inline commands capped at 64 KB.
- **Sanitizer Verification**: 100% clean under AddressSanitizer (ASan), UndefinedBehaviorSanitizer (UBSan), and ThreadSanitizer (TSan).
