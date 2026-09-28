# MiniRedis Technical Interview Guide: 20 Core Systems Questions

A curated question-and-answer guide for SDE internship and software engineering interviews, derived directly from the architecture and implementation of MiniRedis.

---

### Category 1: Architecture & Network I/O

#### 1. Why is Redis (and MiniRedis) designed around a single-threaded event loop rather than a thread-per-connection model?
> **Answer**: 
> 1. **Zero Synchronization Overhead**: Memory lookups take ~100ns. Multithreaded architectures require fine-grained mutexes or spinlocks across hash buckets, introducing lock contention, cache-line bouncing, and thread context switching that exceed the actual execution time.
> 2. **Deterministic Execution**: Predictable serialization eliminates race conditions in read-modify-write patterns (`INCR`, `EXPIRE`, conditional `SET`).
> 3. **I/O Bound, Not CPU Bound**: With non-blocking I/O multiplexing (`epoll`/`kqueue`), one thread can service hundreds of thousands of concurrent connections because in-memory operations are fast enough that network transfer is the true bottleneck.

#### 2. What is the fundamental difference between `select`/`poll` and `epoll`/`kqueue`?
> **Answer**:
> - `select` and `poll` have $\mathcal{O}(N)$ complexity where $N$ is the total number of monitored file descriptors. The kernel must iterate through every descriptor on every call, and the application must copy the entire descriptor set back and forth between user and kernel space.
> - `epoll` (Linux) and `kqueue` (macOS/BSD) register descriptors once into a kernel interest list via an internal red-black tree. Readiness events are delivered via a dedicated ready list in $\mathcal{O}(\text{events})$ time. Only active descriptors are returned to user space, maintaining constant-time efficiency even with 100,000+ idle connections.

#### 3. How does edge-triggered (EPOLLET) mode differ from level-triggered mode, and what bug occurs if a non-blocking loop is coded incorrectly under edge-triggering?
> **Answer**:
> - **Level-Triggered**: The OS notifies the event loop repeatedly as long as the file descriptor buffer contains readable data.
> - **Edge-Triggered**: The OS notifies the event loop *only once* when the descriptor transitions from unready to ready (state change).
> - **Trap**: If an edge-triggered socket read does not loop until `read()` returns `-1` with `errno == EAGAIN` or `EWOULDBLOCK`, unconsumed bytes remain trapped in the kernel buffer without triggering another notification, permanently starving that client connection until new data arrives.

#### 4. How did you implement graceful shutdown on `SIGINT` without encountering signal-handler race conditions?
> **Answer**: By implementing the **Unix Self-Pipe Trick**. Signal handlers run asynchronously and cannot safely invoke non-reentrant functions (like `malloc`, C++ standard library calls, or network cleanup). In MiniRedis, the `SIGINT` handler writes a single byte into a non-blocking Unix pipe (`signal_pipe_[1]`). The read end (`signal_pipe_[0]`) is registered with the event loop multiplexer (`epoll`/`kqueue`). The main event loop wakes up synchronously, breaks its loop, and executes structured teardown (closing clients, flushing AOF logs) safely on the main thread.

---

### Category 2: Protocol Design & Parsing

#### 5. Why does Redis use the RESP (REdis Serialization Protocol) instead of JSON or Protocol Buffers?
> **Answer**:
> - **Zero-Ambiguity Framing**: RESP uses explicit type-prefix characters (`+`, `-`, `:`, `$`, `*`) followed by string length prefixes. This allows streaming zero-copy parsers to know the exact byte boundary of messages before scanning payloads, avoiding escaping issues.
> - **Human Readability & Binary Safety**: Unlike JSON (which cannot handle raw binary bytes like null characters without base64 expansion) or Protobuf (which requires pre-compiled schemas and lacks human readability), RESP is completely binary-safe while remaining readable via plaintext tools like `nc`, `telnet`, or `redis-cli`.

#### 6. What is the difference between pipelined commands and inline commands in Redis?
> **Answer**:
> - **Inline Commands**: Plain ASCII strings without RESP array framing, terminated by `\r\n` (e.g. `PING\r\n` or `SET k v\r\n`), designed for interactive telnet debugging.
> - **Pipelined Commands**: A client sends multiple full RESP frames over a single TCP socket connection without waiting for the intermediate responses. The server executes them sequentially and returns replies in a single coalesced packet, amortizing network round-trip time (RTT).

#### 7. How did you protect the RESP parser against malicious integer overflow attacks?
> **Answer**: When parsing length prefixes (`$<len>\r\n`), an attacker can send large integers like `len = 0x7FFFFFFFFFFFFFFF`. In naive code, calculating `total_needed = header_size + len + 2` overflows unsigned 64-bit integers and wraps to a small number, bypassing buffer availability checks and triggering out-of-bounds reads during trailing CRLF validation. MiniRedis mitigates this by validating bounds before arithmetic: it strictly caps bulk string lengths at 512 MB and rejects negative values ($< -1$), preventing arithmetic wrap-around.

---

### Category 3: In-Memory Data Structures & Expiration

#### 8. Why wrap `std::unordered_map` for the primary store, and what are its memory-locality disadvantages compared to Redis's C `dict`?
> **Answer**: `std::unordered_map` provides standard chaining with $\mathcal{O}(1)$ average lookup and clean RAII lifecycle management. However, its node-based allocation (`std::__hash_node`) scatters nodes across the heap, requiring pointer-chasing that incurs CPU L1/L2 cache misses. Redis's custom C `dict` uses contiguous pointer tables and embedded buffers optimized for 64-byte CPU cache lines, significantly improving cache locality under high throughput.

#### 9. How does MiniRedis achieve true $\mathcal{O}(1)$ uniform random sampling for active expiration without scanning all keys?
> **Answer**: A standard hash map cannot be sampled uniformly in $\mathcal{O}(1)$ because random bucket probing hits empty slots at low load factors. MiniRedis pairs `std::unordered_map<string, TimePoint> expires_` with a contiguous vector `std::vector<string> expiry_keys_` and a reverse index map `std::unordered_map<string, size_t> expiry_index_`.
> 1. Generating a random index in $[0, \text{size}-1]$ directly yields an expiring key in $\mathcal{O}(1)$.
> 2. Deletions use the **swap-and-pop idiom**: swap the target key with the back of the vector, update the reverse index, and call `pop_back()`, achieving $\mathcal{O}(1)$ deletion without memory reallocations.

#### 10. Why does Redis use a hybrid passive + active expiration model instead of a background thread sleeping on a timer?
> **Answer**:
> - **Passive (Lazy)**: Expired keys are checked and deleted on access (`GET`, `EXISTS`). This incurs zero background CPU overhead, but keys that are never accessed would leak memory forever.
> - **Active (Probabilistic)**: A periodic cycle samples 20 keys with TTLs. If $>25\%$ of the sample is expired, the cycle repeats immediately, bound by a 25ms time budget. This bounds memory leakage from unaccessed keys while ensuring the event loop never stalls.
> - **No Per-Key Timers**: Having individual timers or a min-heap requires $\mathcal{O}(\log N)$ updates on every `EXPIRE` or `DEL`, whereas the probabilistic model provides constant-time performance across millions of keys.

---

### Category 4: Eviction Algorithms (LRU & LFU)

#### 11. How is exact $\mathcal{O}(1)$ LRU implemented, and why does real Redis use approximated LRU instead?
> **Answer**:
> - **Exact $\mathcal{O}(1)$ LRU (MiniRedis)**: Uses a hash map mapping keys to nodes in a doubly linked list with sentinel head and tail pointers. Node detachment and head attachment take $\mathcal{O}(1)$ on every access. Evicting the tail takes $\mathcal{O}(1)$.
> - **Approximated LRU (Real Redis)**: Maintaining two pointers (prev/next) per key costs 16 bytes of pointer overhead per entry. Redis instead stores a compact 24-bit timestamp inside each object header (`robj::lru`) and samples 5 random keys during eviction, choosing the oldest. This saves tens of megabytes of memory across millions of keys with near-identical hit rates.

#### 12. How does exact $\mathcal{O}(1)$ LFU cache eviction work with frequency tie-breaking?
> **Answer**: MiniRedis implements an $\mathcal{O}(1)$ frequency-bucket architecture:
> 1. A hash map maps keys to nodes.
> 2. A map of frequency buckets (`size_t freq -> DoublyLinkedList`) groups keys with identical access counts.
> 3. A `min_freq` integer tracks the current global minimum frequency.
> - When a key is accessed, its node is spliced from bucket $F$ to $F+1$. If bucket $F$ becomes empty and $F == \text{min\_freq}$, $\text{min\_freq}$ is incremented.
> - For eviction, the victim is detached from the tail of bucket `min_freq`. Because nodes within each frequency bucket are ordered from MRU (head) to LRU (tail), tail eviction provides exact **LRU tie-breaking** among keys sharing the minimum access frequency.

#### 13. How does MiniRedis track memory usage for eviction under the `CONFIG SET maxmemory` constraint?
> **Answer**: MiniRedis uses deterministic memory accounting. Each key-value pair's memory footprint is calculated as:
> $$\text{Footprint} = \text{key.size}() + \text{value.size}() + \text{kEntryOverhead}$$
> where `kEntryOverhead = 64` bytes accounts for hash bucket nodes and tracker pointers. On every mutating write (`SET`), the memory delta is checked against `maxmemory`. If exceeded under `allkeys-lru` or `allkeys-lfu`, the server pops victims in a loop until sufficient bytes are freed. Under `noeviction`, it rejects writes with `-OOM command not allowed when used memory > 'maxmemory'`.

---

### Category 5: Persistence & Crash Recovery (AOF)

#### 14. What are the trade-offs between AOF (Append-Only File) and RDB (Redis Database Snapshot)?
> **Answer**:
> - **RDB**: Point-in-time binary snapshot written by a child process via `fork()`. Fast restore times and compact file size, but risks losing 5–15 minutes of data between snapshots. Furthermore, calling `fork()` on huge heaps copies page tables and can trigger severe memory exhaustion under copy-on-write (COW).
> - **AOF**: Continuous log of every mutating command written in RESP format. Durability is bounded to at most 1 second (`everysec`), but logs grow larger and require periodic compaction (`BGREWRITEAOF`).

#### 15. How does MiniRedis handle relative TTLs during AOF logging and replay so expired keys are never resurrected?
> **Answer**: Relative timeouts (e.g. `SET k v EX 10`) cannot be logged directly; replaying that log on reboot 2 hours later would erroneously resurrect the key for another 10 seconds. MiniRedis translates all relative timeouts to absolute Unix epoch timestamps (`PEXPIREAT key <timestamp_ms>`) before appending to the AOF. On startup replay, `PEXPIREAT` calculates remaining time relative to the current monotonic/system clock. If the timestamp has passed, the key is discarded immediately.

#### 16. How does AOF log compaction achieve atomic replacement without risking log corruption during a crash?
> **Answer**: By using the **POSIX atomic rename primitive**:
> 1. The rewrite engine scans the current in-memory store and generates a minimal set of `SET` and `PEXPIREAT` commands.
> 2. The commands are written to a temporary file: `<filename>.rewrite.tmp`.
> 3. An explicit `fsync()` flushes all OS dirty pages to physical disk.
> 4. `::rename("<filename>.rewrite.tmp", "<filename>")` atomically replaces the active log. If power fails at any point prior to rename, the original AOF log remains intact and uncorrupted.

#### 17. How does MiniRedis handle a crash that truncates the tail of the AOF log mid-write?
> **Answer**: On startup, `AofManager::replay()` parses frames iteratively while tracking the byte offset of the last known valid command. If the parser encounters an `Incomplete` or corrupted trailing frame, it detects that the crash occurred during a write syscall. It logs a warning, calls `ftruncate(fd, last_valid_offset)` to prune the corrupted tail, and resumes normal operation without requiring manual operator intervention.

---

### Category 6: Sanitizers, Concurrency & Performance

#### 18. Why can't AddressSanitizer (ASan) and ThreadSanitizer (TSan) be compiled into the same binary build?
> **Answer**: ASan and TSan instrument memory accesses with conflicting shadow memory mappings. ASan maps 1 byte of shadow memory for every 8 bytes of application memory to track validity and redzones. TSan maps multiple shadow words for every memory address to track thread IDs, vector clocks, and access types (read vs write) to detect data races. Because both sanitizers intercept memory allocators and require exclusive control over virtual address ranges, compilers disallow combining them into a single build.

#### 19. Why does real Redis outperform MiniRedis by $10\times$ in pipelined benchmarks, but only by $1.5\times$ in non-pipelined benchmarks?
> **Answer**:
> - In non-pipelined tests (`-P 1`), network round-trip time (syscalls, TCP stack traversal) consumes $>85\%$ of execution time, masking low-level CPU differences and keeping MiniRedis at 71% of Redis throughput (111,000 ops/sec).
> - Under pipelining (`-P 16`), network latency is amortized, making CPU execution and memory throughput the bottleneck. Real Redis reaches 2M ops/sec because it uses preallocated static reply buffers (`shared.ok`), cursor-based buffer parsing (`qb_pos`), and jemalloc-optimized structs with zero heap allocations on the hot path. MiniRedis creates heap `std::string` allocations and shifts buffers via `std::string::erase`, incurring allocator overhead.

#### 20. If you were taking MiniRedis to production, what would be the top 3 architectural changes you would make?
> **Answer**:
> 1. **Zero-Copy Static Buffer Pipeline**: Replace `std::string::erase` with read cursor pointers (`std::string_view`) and preallocate static RESP replies (`shared.ok`, `shared.pong`) to eliminate `malloc`/`free` calls during request processing.
> 2. **Progressive Rehashing**: Replace `std::unordered_map` with a dual-table dictionary that migrates buckets incrementally during regular commands to eliminate latency spikes during table resizing.
> 3. **Threaded I/O Multiplexing (Redis 6 Model)**: Offload socket read/write operations and RESP parsing to background worker threads while keeping core database state mutations strictly single-threaded, unlocking multi-core network throughput while preserving lock-free consistency.
