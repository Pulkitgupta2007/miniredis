# MiniRedis: One-Page Technical Interview Cheatsheet

---

## 1. Command Time & Space Complexities

| Command | Time Complexity | Space Complexity | Description & Notes |
| :--- | :--- | :--- | :--- |
| **`GET key`** | $O(1)$ avg / $O(N)$ worst | $O(1)$ | Hash lookup + lazy passive expiration check + LRU/LFU touch. |
| **`SET key val [EX/PX] [NX/XX]`** | $O(1)$ avg | $O(\text{len}(k) + \text{len}(v))$ | Updates hash map, adds to TTL index, updates eviction tracker, writes to AOF. |
| **`DEL key [key ...]`** | $O(M)$ where $M$ is # of keys | $O(1)$ | Removes from storage, frees memory accounting, unlinks from LRU/LFU. |
| **`EXPIRE key sec`** | $O(1)$ | $O(1)$ | Sets `expires_at = now() + sec` and adds key to probabilistic expiry index. |
| **`TTL key` / `PTTL key`** | $O(1)$ | $O(1)$ | Returns remaining life in sec/ms; returns `-2` if missing, `-1` if persistent. |
| **`KEYS pattern`** | $O(N)$ where $N$ is total keys | $O(N)$ return buffer | **Blocking operation**: iterates entire keyspace. Not recommended in prod. |
| **`INFO [section]`** | $O(1)$ | $O(1)$ | Formats atomic telemetry metrics (ops/sec, hit ratio, memory usage). |
| **`CONFIG SET/GET`** | $O(1)$ | $O(1)$ | Dynamic runtime reconfiguration of `maxmemory` and `maxmemory-policy`. |
| **`PING [msg]`** | $O(1)$ | $O(1)$ | Heartbeat validation; returns `+PONG\r\n` or bulk string echo. |
| **`FLUSHDB`** | $O(N)$ | $O(1)$ | Empties all tables, resets memory counters, clears LRU/LFU state. |

---

## 2. Core Data Structures & Memory Overhead

| Subsystem | Data Structure Used | Time Complexity | Memory Overhead |
| :--- | :--- | :--- | :--- |
| **Primary Keyspace** | `std::unordered_map<string, Entry>` | $O(1)$ lookup / insert | 24–32B pointer overhead + bucket table (8B/slot). |
| **Active TTL Index** | `vector<string>` + `map<string, size_t>` | $O(1)$ random sample, $O(1)$ swap-and-pop delete | 8B/entry in vector + 32B/entry in index map. |
| **Exact LRU** | `std::list<string>` + `map<string, list::iter>` | $O(1)$ access (via `splice`), $O(1)$ pop tail | 24B (prev/next pointers) + 32B map node per key. |
| **Exact LFU** | `map<uint64, list<string>>` + `min_freq_` | $O(1)$ access, $O(1)$ pop min freq | Frequency bucket map + 24B list node per key. |
| **Eviction Events** | Lock-free Ring Buffer (`std::atomic<uint64>`) | $O(1)$ write, $O(1)$ read | Fixed 64KB static buffer (`1024 * 64B`). Zero heap allocations. |

---

## 3. Eviction & Persistence Trade-Off Matrix

### Eviction Policies
* **MiniRedis Exact LRU**: `std::list::splice` provides true $O(1)$ recency ordering. Guarantees 0 false evictions. Cost: 56 bytes pointer overhead per key.
* **MiniRedis Exact LFU**: Frequency buckets with `min_freq_` pointer break ties via FIFO. Protects hot keys. Cost: Higher pointer overhead; vulnerable to stale hot keys without decay.
* **Real Redis Approximated LRU**: Samples $K$ keys (default 5) using a 24-bit object header timestamp (`lruclock`). Cost: Only 3 bytes per key (18× less memory), but 1–2% suboptimal eviction order.

### AOF Persistence Policies
* **`always`**: `::write()` + immediate `::fsync()`. Maximum durability (0 data lost). Throughput: ~1k ops/sec (disk bound).
* **`everysec`**: Main thread calls `::write()`; background worker thread calls `::fsync()` every 1s. Durability: $\le 1\text{s}$ lost. Throughput: ~100k ops/sec.
* **`no`**: `::write()` only; kernel flushes page cache opportunistically (up to 30s). Durability: up to 30s lost. Throughput: >150k ops/sec.

---

## 4. Key Linux / POSIX System Calls

| System Call | Purpose in MiniRedis | Critical Behavior / Flags |
| :--- | :--- | :--- |
| `epoll_create1(0)` / `kqueue()` | Initializes multiplexer instance | Returns reactor file descriptor. |
| `epoll_ctl()` / `kevent()` | Registers / modifies socket interest | Adds `EPOLLIN` / `EVFILT_READ`, enables `EPOLLOUT` on write backpressure. |
| `fcntl(fd, F_SETFL, O_NONBLOCK)` | Makes sockets non-blocking | Prevents `accept()`, `read()`, and `write()` from hanging the event loop. |
| `rename(tmp_aof, live_aof)` | Atomic AOF swap during rewrite | **POSIX atomic**: guarantees file integrity even if crash occurs during rewrite. |
| `ftruncate(fd, valid_bytes)` | AOF crash recovery | Truncates corrupted or partial trailing RESP commands on startup. |
| `pipe2(fds, O_NONBLOCK)` | Signal handling self-pipe | Allows `SIGINT`/`SIGTERM` to wake reactor loop without async-signal-unsafe calls. |

---

## 5. Architectural Constants & Defensive Guardrails

* **Max Bulk String**: `536,870,912` bytes (512 MB) — bounds single RESP token parsing.
* **Max Command Arguments**: `1,024` tokens — prevents speculative vector allocation attacks (`*1000000000\r\n`).
* **Max Client Input Buffer**: `67,108,864` bytes (64 MB) — closes slowloris/hung client connections.
* **Active Expiration Sampling**: `20` keys per cycle, repeats if $>25\%$ expired, capped at **25ms max budget**.
* **Estimated Memory Overhead**: `64` bytes added to `key.size() + value.size()` for exact tracking.

---

## 6. MiniRedis vs Real Redis

| Dimension | MiniRedis (What I Built) | Real Redis |
| :--- | :--- | :--- |
| **I/O Engine** | C++17 single-threaded reactor (`epoll`/`kqueue`) | C single-threaded reactor (`ae.c` wrapping epoll/kqueue) |
| **Eviction Engine** | **Exact $O(1)$** LRU (`std::list`) & LFU (Bucket map) | **Approximated** LRU/LFU (24-bit header + random 5-key sample) |
| **Persistence** | RESP-based AOF (`always`, `everysec`, `no`) + truncation recovery | Dual: AOF (`appendfsync`) + RDB (point-in-time binary snapshot) |
| **AOF Rewrite** | Foreground iteration + atomic POSIX `rename()` | Background `fork()` with Copy-on-Write (CoW) page sharing |
| **Telemetry / UI** | Custom Node.js bridge + real-time React SVG dashboard | Redis CLI + Commercial RedisInsight |
| **Data Types** | Binary-safe Strings (`SET`, `GET`, `DEL`, `EXPIRE`) | Strings, Lists, Hashes, Sets, Sorted Sets (SkipList), Streams |

---

## 7. 30-Second Interview Elevator Pitch

> *"I built MiniRedis, a high-performance, in-memory key-value database in C++17 modeled on Redis. It features a non-blocking single-threaded reactor supporting Linux `epoll` and macOS `kqueue`, a zero-copy streaming RESP2 parser, configurable AOF persistence with crash recovery, and exact $O(1)$ LRU and LFU eviction engines. Under benchmarking, it achieves 111,000 ops/sec with sub-millisecond p99 latencies, validated through 59 sanitizing unit tests and a full-stack real-time telemetry dashboard."*

### What NOT to Claim:
* ❌ *"It is faster than Redis"* $\to$ **Say instead**: *"It achieves ~56–71% of Redis's raw throughput on single-command operations, but Redis outperforms it on pipelining due to custom jemalloc memory arenas and zero C++ std::string allocations."*
* ❌ *"It is production-ready for distributed clusters"* $\to$ **Say instead**: *"It is an ACID-decoupled single-node caching engine designed to master systems programming, socket I/O multiplexing, and exact memory eviction algorithms."*
