# MiniRedis: Technical Interview Preparation Guide

This guide contains the complete technical preparation materials for the **MiniRedis** project (a Redis-inspired in-memory key-value database in C++17 with non-blocking epoll/kqueue, RESP2 protocol, TTL expiration, exact $O(1)$ LRU/LFU cache eviction, AOF persistence, telemetry metrics, a Node.js bridge, and a real-time React dashboard).

---

## 1. Repository Blueprint & Architecture Map

```text
miniredis/
├── server/                          # C++17 Core Storage & Network Engine
│   ├── include/miniredis/           # Public Headers & Core Data Structures
│   │   ├── poller.h                 # Unified Reactor Poller (Linux epoll / macOS kqueue)
│   │   ├── server.h                 # Non-blocking TCP server, connection session map
│   │   ├── resp.h                   # RESP2 protocol types, frame parser & serializer
│   │   ├── command.h                # Command dispatcher, syntax parsing, argument routing
│   │   ├── store.h                  # Primary key-value engine, TTL, memory limits, eviction
│   │   ├── lru_tracker.h            # Exact O(1) LRU (std::list + std::unordered_map)
│   │   ├── lfu_tracker.h            # Exact O(1) LFU (Frequency buckets + min_freq pointer)
│   │   ├── chained_hash_table.h     # Custom power-of-two separate chaining hash table
│   │   ├── aof.h                    # Append-Only File persistence & rewrite manager
│   │   ├── telemetry.h              # Atomic ops/sec, hit ratio, command counters
│   │   └── eviction_ring_buffer.h   # Lock-free ring buffer for eviction & expiry events
│   ├── src/                         # Implementation Files
│   │   ├── main.cpp                 # CLI entry point (port, host, config initialization)
│   │   ├── server.cpp               # Event loop, non-blocking accept/read/write, signal pipe
│   │   ├── poller.cpp               # epoll_wait (Linux) / kevent (macOS) multiplexing
│   │   ├── resp.cpp                 # Zero-copy std::string_view streaming RESP parser
│   │   ├── command.cpp              # SET/GET/DEL/EXPIRE/CONFIG/INFO handler dispatch
│   │   ├── store.cpp                # In-memory mutations, active probabilistic expiry loop
│   │   └── aof.cpp                  # write(), fsync background thread, fork() CoW rewrite
│   └── tests/                       # 59 GoogleTest unit tests & RESP fuzz suite
├── bridge/                          # Node.js WebSocket & HTTP Telemetry Bridge
│   ├── src/
│   │   ├── server.js                # HTTP reverse proxy & WebSocket broadcast server
│   │   ├── redis-client.js          # Persistent RESP2 TCP client with auto-reconnect
│   │   └── parser.js                # INFO string, EVENTS string, and frame parsers
│   └── test/                        # 13 Node.js test runner unit & E2E tests
└── dashboard/                       # React 18 + Vite Real-Time Observability UI
    ├── src/
    │   ├── App.jsx                  # WebSocket state, burst-resistant history buffer
    │   ├── App.css                  # Responsive dark mode CSS, CSS Grid containment
    │   └── components/
    │       ├── Header.jsx           # Connection state, uptime, connected clients
    │       ├── MetricCards.jsx      # Live ops/sec, hit ratio, memory percentage
    │       ├── MemoryOpsChart.jsx   # Responsive SVG time series with hover HUD
    │       ├── LruVisualizer.jsx    # Interactive LRU linked list & LFU buckets
    │       ├── EventFeed.jsx        # Live eviction/expiration ring buffer stream
    │       ├── KeyspaceExplorer.jsx # Table browser, TTL ticker, binary-safe CRUD
    │       ├── TrafficPlayground.jsx# Traffic generator (Burst, Eviction Storm, Flush)
    │       └── CommandConsole.jsx   # Interactive CLI console with syntax helpers
    └── test/                        # 23 Vitest component tests
```

---

## 2. End-to-End Command Flow: `SET foo bar EX 10`

When a client sends `SET foo bar EX 10`, the command traverses the entire stack:

```text
[ Client Socket ]
       │
       ▼  Raw TCP bytes: *4\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n$2\r\nEX\r\n$2\r\n10\r\n
[ Poller::poll() ] (epoll_wait / kevent)
       │  Wakes up event loop: client_fd is readable
       ▼
[ Server::handle_client_read(fd) ]
       │  ::read(fd, buf, 16384) -> Appends bytes to client.in_buf
       ▼
[ RespParser::parse_command() ]
       │  Validates array prefix (*4), parses bulk strings, extracts tokens:
       │  args = ["SET", "foo", "bar", "EX", "10"], consumes N bytes
       ▼
[ CommandDispatcher::execute() ]
       │  Validates syntax, parses "EX 10" via std::from_chars into chrono::seconds(10)
       ▼
[ Store::set("foo", "bar", 10s, SetCondition::NONE) ]
       │  1. Passive lazy expiration: check_and_expire("foo")
       │  2. Memory check: estimate_memory("foo", "bar") -> 3 + 3 + 64 = 70 bytes
       │  3. Eviction: If used_memory_ + 70 > maxmemory_, evict via lru_tracker_.pop_lru()
       │  4. Trackers: lru_tracker_.add("foo") (move to head), lfu_tracker_.add("foo")
       │  5. Expiry: expires_["foo"] = now() + 10s, indexed in expiry_keys_
       │  6. Store: data_["foo"] = Entry{"bar", expires_at}
       │  7. Returns SetResult{ok: true, oom: false}
       ▼
[ CommandDispatcher ]
       │  Serializes reply: RespSerializer::serialize_simple_string("OK") -> "+OK\r\n"
       ▼
[ Server::handle_client_read ]
       │  Checks if command is mutating: logs to AOF
       ▼
[ AofManager::append_command() ]
       │  Writes RESP array to AOF file descriptor via ::write()
       │  If FsyncPolicy::ALWAYS -> immediate ::fsync(fd_)
       │  If FsyncPolicy::EVERYSEC -> background thread fsyncs on 1s timer
       ▼
[ Server::handle_client_write() ]
       │  Writes "+OK\r\n" to client socket (or buffers in client.out_buf if EAGAIN)
       ▼
[ Client receives "+OK\r\n" ]
```

---

## 3. Core Architectural Subsystems

### A. Non-Blocking I/O & The Reactor Pattern
* **Abstraction:** [`Poller`](file:///Users/pulkit/miniredis/server/include/miniredis/poller.h) unifies Linux `epoll` and macOS `kqueue`.
* **Level-Triggered (LT) Mode:** Chosen over Edge-Triggered (ET) because it is immune to event loss if a read buffer is partially consumed.
* **Non-Blocking Sockets (`O_NONBLOCK`):** Set on all listening and accepted client sockets. Prevents thread stalls on aborted TCP handshakes (`RST`) or saturated send buffers (`EAGAIN`).
* **The Self-Pipe Trick:** [`Server::init_signal_pipe()`](file:///Users/pulkit/miniredis/server/src/server.cpp#L48) creates a non-blocking UNIX pipe for `SIGINT`/`SIGTERM`. Signal handlers only call `write(pipe[1], "x", 1)` (strictly async-signal-safe). The event loop wakes up on `pipe[0]`, ensuring a clean shutdown with zero data races.

### B. RESP2 Wire Protocol & Zero-Copy Parsing
* **Zero-Copy Views:** [`RespParser`](file:///Users/pulkit/miniredis/server/src/resp.cpp) inspects `std::string_view` slices over socket read buffers.
* **Fragmentation Handling:** If a command is partially received, the parser returns `ParseStatus::Incomplete` and consumes zero bytes; the server leaves `client.in_buf` intact until more fragments arrive.
* **Pipelining:** Multiple commands in a single TCP read buffer are parsed and executed in a tight loop without waiting for individual client round-trips.
* **DoS Hardening:** Maximum bulk string length capped at **512 MB**, per-client buffer ceiling capped at **64 MB**, and maximum recursion depth capped at **64 levels**.

### C. Storage & Hash Table Design
* **Primary Store:** [`std::unordered_map<std::string, Entry> data_`](file:///Users/pulkit/miniredis/server/include/miniredis/store.h) provides $O(1)$ amortized lookups.
* **Custom Chained Hash Table:** [`ChainedHashTable`](file:///Users/pulkit/miniredis/server/include/miniredis/chained_hash_table.h) implements separate chaining with power-of-two sizing, bitwise bucket masking (`hash & (cap - 1)`), and load factor dynamic resizing.
* **Real Redis Contrast:** Real Redis uses **progressive incremental rehashing** over two tables (`dictht[2]`), migrating 1 bucket per command to eliminate latency spikes.

### D. Key Expiration Engine (TTL)
* **Passive (Lazy) Expiration:** On every access (`get`, `ttl`, `exists`), `check_and_expire()` deletes the key if `now >= expires_at`.
* **Active Probabilistic Expiration:**
  * Iterating all keys is $O(N)$ and would freeze the server.
  * Instead, [`Store::active_expire_cycle()`](file:///Users/pulkit/miniredis/server/src/store.cpp#L270) samples **20 random keys** with expiration in $O(1)$ time using `expiry_keys_` vector swap-and-pop.
  * If **more than 25% (5 keys)** are expired, it repeats immediately.
  * Enforces a hard **25ms wall-clock ceiling** to prevent starving network clients.
* **Injectable Clock:** `Store` accepts a `ClockFn` lambda, enabling deterministic time advancement in unit tests without `sleep()`.

### E. Cache Eviction Policies: LRU & LFU
* **Exact $O(1)$ LRU ([`LruTracker`](file:///Users/pulkit/miniredis/server/include/miniredis/lru_tracker.h)):**
  * `std::list<std::string>` (Head = MRU, Tail = LRU) + `std::unordered_map<string, list::iterator>`.
  * On access: `list_.splice(list_.begin(), list_, it)` moves the node to head in $O(1)$.
  * On eviction: pops the tail node in $O(1)$.
  * *Redis Contrast:* Real Redis avoids pointer overhead by using **approximated LRU**: a 24-bit timestamp in `robj` + random sampling of 5 keys.
* **Exact $O(1)$ LFU ([`LfuTracker`](file:///Users/pulkit/miniredis/server/include/miniredis/lfu_tracker.h)):**
  * Frequency buckets (`unordered_map<int, list<Node>>`) + key map + `min_freq_` pointer.
  * On access: unlinks node from bucket $f$, pushes to bucket $f + 1$. Advances `min_freq_` if bucket $f$ becomes empty.
  * On eviction: pops tail of bucket `min_freq_` (LRU tie-breaking within lowest frequency).
  * *Redis Contrast:* Real Redis uses an **8-bit Morris logarithmic counter** inside `robj->lru`.

### F. AOF Persistence & Crash Recovery
* **Three Fsync Policies:**
  1. `always`: Synchronous `fsync()` on every write (maximum durability, disk-bound throughput).
  2. `everysec`: Background worker thread syncs once per second (optimal balance, 1s data loss window).
  3. `no`: OS handles dirty page cache flushing (highest throughput, up to 30s data loss window).
* **Compaction (`BGREWRITEAOF`):**
  * Iterates database snapshot and writes canonical `SET` and `PEXPIREAT` commands to `.rewrite.tmp`.
  * Atomically replaces the active log using POSIX `rename()`, preventing file corruption if the server crashes mid-write.
* **Auto-Truncation Recovery:** [`AofManager::replay()`](file:///Users/pulkit/miniredis/server/src/aof.cpp#L106) detects truncated partial frames from sudden power loss and uses POSIX `::truncate()` to restore the log to the last valid byte offset.
* **Absolute Timestamps:** Expirations are logged as absolute epoch milliseconds (`PEXPIREAT`), preventing TTL resurrection on restart.

---

## 4. Benchmark Performance & Profiling Analysis

Benchmarked against official Redis on Apple Silicon (arm64, 50 client connections, loopback TCP):

| Workload | MiniRedis | Real Redis | Performance Ratio |
|---|---|---|---|
| **SET (non-pipelined, `-P 1`)** | 90,909 ops/s | 161,290 ops/s | **56.4%** |
| **GET (non-pipelined, `-P 1`)** | 111,607 ops/s | 156,250 ops/s | **71.4%** |
| **SET (pipelined, `-P 16`)** | 161,290 ops/s | 1,470,588 ops/s | **11.0%** |
| **GET (pipelined, `-P 16`)** | 204,918 ops/s | 2,083,333 ops/s | **9.8%** |

### Why MiniRedis is Slower Under Heavy Pipelining:
1. **Heap Allocation Churn:** MiniRedis allocates `std::string` objects for parsed tokens and replies; real Redis uses pre-allocated static response buffers (`shared.ok`, `c->buf`).
2. **Buffer Shift Overhead:** MiniRedis calls `client.in_buf.erase(0, consumed)` (triggering `memmove` per command in a pipeline); real Redis advances an offset cursor pointer (`c->qb_pos`).
3. **Cache Locality:** `std::unordered_map` uses heap-allocated node buckets; real Redis uses contiguous C `dict` tables with Jemalloc.
4. **LRU/LFU Bookkeeping:** MiniRedis updates linked-list pointers on read queries; real Redis writes an inline 24-bit timestamp.

---

## 5. Interview Delivery Cheat Sheet

### 30-Second Elevator Pitch
> *"I built **MiniRedis**, an in-memory key-value database in C++17 inspired by Redis. It features a non-blocking reactor event loop multiplexing sockets via Linux `epoll` and macOS `kqueue`, full RESP2 protocol compliance, and a dual expiration engine combining lazy checks with active probabilistic sampling. It implements exact $O(1)$ LRU and LFU cache eviction, AOF persistence with background compaction, and passes all tests under AddressSanitizer and ThreadSanitizer. It achieves over 111,000 ops/second with sub-millisecond p99 latency."*

### Honest Answer: "Did you use AI?"
> *"Yes, I used AI as an accelerator and pair programmer. However, the systems architecture, debugging, and verification were strictly mine. For example, I rejected AI's suggestion to use min-heaps for TTL because heaps cannot do $O(1)$ random sampling; I identified and patched integer overflow vulnerabilities in the parser under fuzzing; and I diagnosed a CSS Grid blowout bug that caused the frontend graph to explode on eviction bursts. I understand every single system call, data structure, and line of code in this repository."*
