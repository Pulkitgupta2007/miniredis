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

---

## 5. Eviction Implementation: Exact $\mathcal{O}(1)$ LRU/LFU vs. Redis Approximated Sampling

- **Option A (Redis Approximated LRU/LFU)**:
  Redis does not maintain a doubly linked list or frequency buckets. Instead, every Redis object stores a 24-bit timestamp/counter in `robj::lru`. When memory is full, Redis samples 5 keys at random and evicts the one with the oldest access time / lowest frequency counter.
  - *Pros*: Saves 16 to 24 bytes of pointer overhead per key across millions of entries.
  - *Cons*: Eviction is not mathematically exact. Hot keys can rarely be evicted if they happen to be in an unluckily sampled batch; access sequences are not strictly deterministic.
- **Option B (Exact $\mathcal{O}(1)$ Doubly-Linked LRU & Frequency-Bucket LFU - MiniRedis Implementation)**:
  Maintains explicit hash maps and doubly linked list nodes for LRU, and frequency buckets with a `min_freq` pointer for LFU.
  - *Pros*:
    - Strictly deterministic eviction order matching classical cache algorithm proofs.
    - True $\mathcal{O}(1)$ worst-case time for insertions, hits, and evictions.
    - Direct interview explainability (classic LeetCode 146 LRU & 460 LFU systems patterns).
  - *Cons*: Node pointers require extra heap memory per tracked key.

---

## 6. Persistence & Durability: AOF Fsync Policies & AOF vs. RDB

### Fsync Policies Trade-Off Matrix

| Policy | Durability Guarantee | Write Latency | Performance Impact | Use Case |
|---|---|---|---|---|
| **`always`** | Maximum (0 data loss) | High ($\sim 1\text{ ms} - 10\text{ ms}$) | Bound by disk IOPS; throughput drops $10\times - 50\times$ | Financial / billing records |
| **`everysec`** | High ($\le 1\text{ second}$ loss) | Minimal ($\sim \mu\text{s}$) | Background thread calls `fsync()` every 1s; near-memory speed | **Recommended production default** |
| **`no`** | Weak (up to 30s loss) | None ($\sim \mu\text{s}$) | OS kernel determines page cache flushing (`pdflush` / `flush-dirty`) | Non-critical cache / read-heavy buffers |

### AOF (Append-Only File) vs. RDB (Snapshotting)

- **RDB Snapshotting**:
  Dumps the entire database state to disk as a binary point-in-time snapshot (via `fork()`).
  - *Pros*: Compact single-file format, fast full-database restore times, zero disk I/O overhead on main client execution.
  - *Cons*: High data loss window (e.g. 5–15 minutes between snapshots); `fork()` on huge heaps copies page tables and can incur severe memory spikes under copy-on-write (COW).
- **AOF Logging**:
  Appends every mutating command to an operational log.
  - *Pros*: Granular durability (at most 1 second of loss under `everysec`), human-readable/parseable RESP format, simple crash recovery.
  - *Cons*: Log files grow larger than RDB files; requires periodic compaction (`rewrite`) to eliminate redundant operations.

### Decision
MiniRedis implements AOF with configurable `always`, `everysec`, and `no` policies, accompanied by atomic log compaction via POSIX `rename()`. This gives users explicit control over the durability/latency frontier.

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

---

## 7. Robustness, Fuzzing, and Sanitizer Hardening

A high-performance in-memory database must be resilient against adversarial inputs, socket fragmentation, and concurrency races. During Phase 8 fuzzing and sanitizer audits, several vulnerabilities and edge-case behaviors were identified, analyzed, and mitigated:

### 1. Integer Overflow in Bulk String Length Calculations
- **Vulnerability**: In RESP, a bulk string length prefix `$len\r\n` can supply arbitrary 64-bit numbers. Without validation, evaluating `size_t total_needed = header_size + static_cast<size_t>(len) + 2` can cause unsigned integer overflow if `len > SIZE_MAX - header_size - 2`. The wrapped value resulted in a small `total_needed`, allowing incomplete inputs to bypass size validation and trigger out-of-bounds reads during CRLF validation (`input[header_size + len]`).
- **Resolution**:
  - Enforced an upper limit: `len > 512 * 1024 * 1024` (512 MB, matching Redis's `proto-max-bulk-len`) returns `Protocol error: bulk string length exceeds 512MB limit`.
  - Enforced that any `len < -1` returns `Protocol error: negative bulk string length`.

### 2. Speculative Heap Exhaustion via Massive Array Allocations
- **Vulnerability**: Fuzz testing with `*1000000000\r\n` (1 billion elements) caused `elements.reserve(static_cast<size_t>(count))` to request tens of gigabytes of RAM upfront before receiving any actual elements, throwing `std::bad_alloc` and crashing the server.
- **Resolution**:
  - Implemented `kMaxArrayElements = 1024 * 1024` (1 million elements limit).
  - Clamped the initial reservation: `elements.reserve(std::min(static_cast<size_t>(count), static_cast<size_t>(1024)))`. Memory grows incrementally as valid sub-frames are parsed.

### 3. Slowloris / Memory Bloat via Incomplete Frame Streams
- **Vulnerability**: An attacker sending gigabytes of random bytes without newlines (`\n`) or completing RESP frames caused the per-client `in_buf` string to expand indefinitely, eventually triggering the OS out-of-memory killer (OOM).
- **Resolution**:
  - Added an inline command limit: incomplete inline commands exceeding 64 KB are rejected immediately with a protocol error.
  - Added a global per-client buffer ceiling: if `client.in_buf.size() > 64 * 1024 * 1024` (64 MB) and no complete command can be parsed, the server returns `-ERR Protocol error: request buffer exceeded maximum limit` and closes the socket.
  - Added zero-byte consumption guard: if a command reports `bytes_consumed == 0`, the buffer is cleared to prevent infinite event loop spins.

### 4. Stack Overflow Protection via Recursion Limits
- **Vulnerability**: Deeply nested arrays (`*1\r\n*1\r\n...*1\r\n+leaf\r\n`) cause recursion in `RespParser::parse_internal`. Excessive nesting depths can exhaust the thread call stack and trigger `SIGSEGV`.
- **Resolution**:
  - Enforced `kMaxRecursionDepth = 64`. If recursion depth reaches 64, parsing aborts with `Protocol error: maximum recursion depth exceeded`.

### 5. Sanitizer Audit Matrix
MiniRedis was subjected to continuous verification across three LLVM sanitizer suites:
- **AddressSanitizer (ASan)** (`-fsanitize=address`): Proves zero heap use-after-free, zero heap buffer overflows, zero global buffer overflows, and zero memory leaks.
- **UndefinedBehaviorSanitizer (UBSan)** (`-fsanitize=undefined`): Proves zero signed integer overflows, zero null pointer dereferences, zero misaligned pointer accesses, and zero invalid bit-shifts.
- **ThreadSanitizer (TSan)** (`-fsanitize=thread`): Validated the AOF engine with its background 1 Hz `fsync` worker thread under the `everysec` policy; proves zero data races between the event loop writer and background sync thread.

---

## 8. Comparative Performance Benchmark & Bottleneck Analysis

MiniRedis was benchmarked side-by-side against official production Redis using `redis-benchmark` on identical hardware (Darwin arm64 Apple Silicon, 50 parallel client connections, loopback TCP):

| Workload | Metric | Real Redis | MiniRedis | Ratio (Mini / Real) |
|---|---|---|---|---|
| **SET (non-pipelined, -P 1)** | Throughput | 161,290 ops/s | 90,909 ops/s | **56.4%** |
| | P50 Latency | 0.143 ms | 0.527 ms | - |
| | P99 Latency | 1.263 ms | 1.071 ms | - |
| **GET (non-pipelined, -P 1)** | Throughput | 156,250 ops/s | 111,607 ops/s | **71.4%** |
| | P50 Latency | 0.159 ms | 0.431 ms | - |
| | P99 Latency | 0.495 ms | 0.871 ms | - |
| **SET (pipelined, -P 16)** | Throughput | 1,470,588 ops/s | 161,290 ops/s | **11.0%** |
| | P50 Latency | 0.455 ms | 4.855 ms | - |
| | P99 Latency | 0.711 ms | 9.711 ms | - |
| **GET (pipelined, -P 16)** | Throughput | 2,083,333 ops/s | 204,918 ops/s | **9.8%** |
| | P50 Latency | 0.319 ms | 3.783 ms | - |
| | P99 Latency | 0.479 ms | 7.567 ms | - |

### Where and Why MiniRedis is Slower (Honest Architectural Analysis)

#### 1. Competitive in Non-Pipelined RTT-Bound Workloads (56% - 71% of Redis)
In non-pipelined mode (`-P 1`), every single operation requires a round-trip network transit (Client $\to$ Loopback $\to$ Server $\to$ Loopback $\to$ Client). Because network syscall overhead (`read`, `write`, `epoll`/`kqueue`) dominates the ~100ns in-memory hash table lookup, MiniRedis achieves over 90,000–111,000 ops/sec and sub-millisecond P99 latency.

#### 2. The Pipelining Divergence (10% - 11% of Redis)
When pipelining is enabled (`-P 16`), network round trips are amortized across 16 commands per TCP frame. At this point, the bottleneck transitions completely from network I/O to pure CPU execution and memory bandwidth. Real Redis hits 1.4M–2.0M ops/sec, while MiniRedis tops out at ~160k–205k ops/sec due to four fundamental design trade-offs:

1. **Heap Allocation Churn on Hot Path (`malloc`/`free`)**:
   - *Real Redis*: Employs static pre-allocated buffer slices for all standard protocol replies (`shared.ok`, `shared.pong`, `shared.colon`, `shared.nullbulk`). Output data is appended directly into a fixed 16 KB static client buffer (`c->buf`), resulting in zero heap allocations for basic SET/GET commands.
   - *MiniRedis*: Constructs heap-allocated `std::string` objects for command arguments (`std::vector<std::string> args`), parsed tokens, and reply strings. Under 2,000,000 ops/sec, the memory allocator (`malloc`/`free`) becomes the primary CPU bottleneck.
2. **Buffer Compaction Overhead (`std::string::erase`)**:
   - *Real Redis*: Maintains a read cursor offset pointer (`c->qb_pos`) into its query buffer, advancing the pointer in $\mathcal{O}(1)$ without moving memory.
   - *MiniRedis*: Calls `client.in_buf.erase(0, bytes_consumed)` after every parsed command. Within a 16-command pipeline, this shifts the remaining buffer contents in memory 16 consecutive times via `memmove()`.
3. **Data Structure Cache Locality (`std::unordered_map` vs. `dict`)**:
   - *Real Redis*: Uses custom C `dict` with contiguous pointer tables tuned for 64-byte CPU cache lines with `jemalloc`.
   - *MiniRedis*: Uses `std::unordered_map` where each key-value entry is wrapped in an individual node (`std::__hash_node`) allocated separately on the heap, resulting in pointer-chasing CPU cache misses.
4. **LRU / LFU Bookkeeping Overhead on Reads**:
   - *Real Redis*: Does not maintain a linked list. Reads simply write a 24-bit clock integer inside the object header (`robj::lru = LRU_CLOCK()`).
   - *MiniRedis*: Fully updates both an explicit doubly-linked list (`lru_tracker_.touch()`) and frequency bucket pointers (`lfu_tracker_.touch()`) on every single `GET`, modifying multiple pointers and dirtying cache lines on what should be read-only lookups.

---

## 9. Deliberate Non-Goals & Architectural Exclusions

To maximize depth over surface area, several production Redis features were deliberately excluded. Here is the rationale behind each non-goal:

### 1. Redis Cluster & Hash Slot Partitioning
- **Why Excluded**: Redis Cluster partitions keys across 16,384 hash slots (`CRC16(key) % 16384`) using gossip protocols for node failure detection, slot migration, and `MOVED`/`ASK` client redirects. Implementing clustering adds substantial distributed systems coordination code (membership tables, gossip messages, quorum leases) that distracts from the core systems engineering goal: mastering low-level socket multiplexing, zero-copy wire protocol parsing, memory management, and cache eviction algorithms.
- **Production Path**: In production, clustering is typically deployed via Envoy proxies, Twemproxy, or client-side consistent hashing ring topologies without altering core single-node storage mechanics.

### 2. Master-Replica Replication (`PSYNC` & Replication Backlog)
- **Why Excluded**: Redis replication maintains a secondary connection stream where the master streams mutating commands alongside replication offset tracking (`REPLCONF ACK`) and a fixed circular replication backlog buffer for partial resynchronization (`PSYNC`). Because MiniRedis already implements continuous write-ahead append logging via AOF, replication would duplicate the serialization logic without adding architectural novelty to the project.

### 3. Additional Composite Data Structures (Lists, Sets, Sorted Sets, Hashes, Streams)
- **Why Excluded**: Production Redis implements dual memory encodings for composite collections to save memory (e.g. `ziplist`/`listpack` for small lists/hashes and `quicklist`/`dict`/`skiplist` for large collections). Adding 5 complex data types expands surface API code by thousands of lines without changing the core storage, networking, or persistence engine. We prioritized implementing custom chained hash tables, exact $\mathcal{O}(1)$ LRU and LFU cache engines, probabilistic active expiration sampling, and a full observability stack for Strings rather than shallow implementations of 20 collections.

### 4. RDB Binary Snapshots (`fork()` Copy-On-Write)
- **Why Excluded**: RDB creates point-in-time binary dumps by calling `fork()` and streaming memory pages to disk while the parent continues servicing requests via OS Copy-on-Write (COW). On modern large-memory systems, `fork()` introduces latency spikes during page table duplication and risks doubling server memory usage under high write throughput. We selected AOF because it provides superior durability ($\le 1\text{ second}$ loss under `everysec` vs 15 minutes under RDB) and verifiable human-readable crash recovery.

### 5. Multi-Threaded I/O (Redis 6 Model)
- **Why Excluded**: Redis 6 introduced auxiliary background worker threads for reading socket input buffers and formatting output buffers, while keeping command execution strictly single-threaded. Single-threaded reactor I/O was chosen to demonstrate mastery of raw non-blocking socket multiplexing (`epoll`/`kqueue`) and lock-free thread safety without the complexity of multi-threaded work-stealing rings.



