# MiniRedis: Technical Interview Q&A (50 Master Questions & Answers)

This document contains 50 rigorous technical interview questions and concise, production-grade answers covering the entire **MiniRedis** architecture, networking engine, storage internals, persistence, and distributed systems trade-offs.

---

## Part 1: Fundamentals & Protocol (Q1 – Q10)

### Q1: What is Redis and why is it so fast?
**Answer:** Redis is an in-memory, key-value data structure store used as a database, cache, streaming engine, and message broker. It achieves sub-millisecond latencies (>100k ops/sec per core) due to three primary design decisions:
1. **Pure in-memory operations**: All reads and writes hit RAM directly, avoiding disk seek and page cache synchronization overhead on the request-response path.
2. **Non-blocking I/O multiplexing**: A single-threaded event loop (`epoll` on Linux, `kqueue` on macOS) multiplexes thousands of concurrent client connections without thread context-switching or lock contention.
3. **Optimized data structures**: Keys and values are backed by memory-efficient, cache-friendly data structures (hash tables, skip lists, ziplists, intsets) tailored for specific data access patterns.

### Q2: How does in-memory storage compare to traditional disk-based databases like PostgreSQL or MongoDB?
**Answer:**
* **Access Latency**: RAM access takes ~50–100 nanoseconds, whereas NVMe SSD random reads take ~10–50 microseconds (100–1000× slower), and spinning disks take ~5–10 milliseconds.
* **Concurrency Model**: Disk engines require complex buffer pool managers, latching protocols (B-Tree page locks), and multi-version concurrency control (MVCC). MiniRedis uses an in-memory single-threaded reactor, eliminating lock overhead entirely.
* **Durability Trade-off**: Disk databases commit directly to Write-Ahead Logs (WAL) before acknowledging writes. MiniRedis decouples execution from persistence, offering configurable AOF fsync intervals (`always`, `everysec`, `no`) where speed can be traded for durability.

### Q3: Why is Redis single-threaded? What are the advantages and drawbacks?
**Answer:**
* **Advantages**:
  1. No locking overhead (no mutexes, read-write locks, or spinlocks on the primary keyspace).
  2. Eliminates thread synchronization bugs (no race conditions, deadlocks, or priority inversions).
  3. No CPU context-switching overhead across kernel threads.
  4. CPU is rarely the bottleneck; memory bandwidth and network I/O reach saturation first.
* **Drawbacks**:
  1. Long-running commands (e.g., `KEYS *`, multi-megabyte `DEL`) block the entire event loop, spiking latency for all connected clients.
  2. Cannot scale vertically across multiple CPU cores on a single instance without running multiple processes or sharding.

### Q4: How does the RESP2 protocol work?
**Answer:** RESP2 (REdis Serialization Protocol) is a binary-safe, human-readable, request-response wire protocol. Frames are determined by their first byte:
* `+` : Simple String (`+OK\r\n`)
* `-` : Error message (`-ERR unknown command\r\n`)
* `:` : 64-bit signed integer (`:1000\r\n`)
* `$` : Bulk String (`$5\r\nhello\r\n`, or `$-1\r\n` for NULL)
* `*` : Array of RESP elements (`*2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n`)

All commands sent from clients are encoded as RESP Arrays of Bulk Strings. Every token specifies its exact byte length followed by `\r\n`, enabling zero-copy parsing and binary safety.

### Q5: What is command pipelining and how does it affect throughput?
**Answer:** Pipelining allows a client to send multiple commands over a single TCP socket sequentially without waiting for the individual reply to each command. 
* **Mechanism**: The server executes commands sequentially from its input buffer and packs the replies into its output buffer.
* **Impact**: Eliminates network Round Trip Time (RTT) between operations and dramatically reduces the number of `read()` and `write()` system calls, increasing throughput from ~100k ops/sec to >500k ops/sec in benchmark scenarios.

### Q6: How does your parser handle partial TCP reads?
**Answer:** TCP is a continuous byte stream without application message boundaries.
* The server reads available bytes from the socket into a per-client buffer (`client.in_buf`).
* `RespParser::parse_command(in_buf, consumed)` parses the buffer using `std::string_view`.
* If a frame is incomplete (e.g., bulk string payload has fewer bytes than declared in the length header, or missing trailing `\r\n`), the parser returns `ParseResult::INCOMPLETE` without modifying `consumed`.
* The server leaves `in_buf` untouched, yields to the event loop, and resumes parsing only when subsequent `read()` calls append the missing bytes.

### Q7: How do you prevent buffer overflow and malicious payload DoS during parsing?
**Answer:** MiniRedis implements three strict defensive guardrails in [`server/src/resp.cpp`](file:///Users/pulkit/miniredis/server/src/resp.cpp):
1. **Bulk String Length Cap**: Declared bulk string lengths are bounded to `MAX_BULK_LEN` (512 MB). Any `$L\r\n` where $L > 536870912$ or $L < -1$ immediately halts parsing and returns an invalid syntax error.
2. **Array Element Cap**: Array element counts are clamped to `MAX_ARGS` (1024) to prevent speculative vector allocation exploits (`*2000000000\r\n`).
3. **Maximum Input Buffer**: If `client.in_buf.size()` exceeds 64 MB without producing a valid executable command, the server closes the connection immediately.

### Q8: What are inline commands vs RESP array commands?
**Answer:**
* **Inline commands**: Plain-text space-delimited commands sent by human operators via raw `telnet` or `nc` (e.g., `PING\r\n` or `SET key val\r\n`).
* **RESP arrays**: Formal protocol frames starting with `*` (e.g., `*3\r\n$3\r\nSET\r\n...`).
* **Handling**: MiniRedis inspects the first byte of `client.in_buf`. If it is not `*`, it switches to `parse_inline_command()`, tokenizes on whitespace up to `\r\n`, and routes arguments into the same execution pipeline.

### Q9: How do `SET foo bar EX 10`, `PX`, `NX`, and `XX` operate together?
**Answer:** Handled in [`server/src/command.cpp`](file:///Users/pulkit/miniredis/server/src/command.cpp) via `SetOptions`:
* `EX <seconds>`: Sets expiration relative to current time in seconds (converted to milliseconds: `sec * 1000`).
* `PX <milliseconds>`: Sets expiration relative to current time in milliseconds.
* `NX`: Only set the key if it does **not** already exist. Returns `$-1\r\n` (null) if key exists.
* `XX`: Only set the key if it **already** exists. Returns `$-1\r\n` (null) if key is missing.
Both conditions and expiry flags are parsed sequentially using `std::from_chars` without memory allocation.

### Q10: How does MiniRedis differ from standard relational database transactions?
**Answer:** MiniRedis does not implement ACID relational transactions or multi-statement rollbacks. Operations are atomic at the single-command level because all commands execute sequentially on the single-threaded event loop. Real Redis provides `MULTI`/`EXEC` for optimistic queuing with `WATCH`, but unlike SQL databases, Redis transactions do not support rollbacks if an individual command fails at runtime due to type mismatch.

---

## Part 2: Systems, Networking & Event Loop (Q11 – Q20)

### Q11: What is I/O multiplexing and why is `epoll`/`kqueue` superior to `select`/`poll`?
**Answer:**
* **`select`/`poll`**: Requires passing an array/bitmask of all monitored file descriptors to the kernel on every single call ($O(N)$ system call cost). The kernel iterates the entire list to check readiness, and the application must iterate all $N$ descriptors to find active ones.
* **`epoll`/`kqueue`**: Uses kernel-managed interest sets (`epoll_ctl` or `kevent`). The kernel registers callbacks with network device drivers. When packets arrive, active file descriptors are placed on an event ready list. `epoll_wait`/`kevent` returns in $O(K)$ time, where $K$ is the number of **active** events, independent of total connections ($N$).

### Q12: Why did you choose Level-Triggered (LT) instead of Edge-Triggered (ET) mode?
**Answer:**
* **Edge-Triggered (ET)** notifications only fire when the underlying readiness state **changes** (e.g., from unreadable to readable). The application must loop `read()` until `EAGAIN`/`EWOULDBLOCK`. If any unread byte remains, the thread hangs indefinitely waiting for new incoming traffic.
* **Level-Triggered (LT)** notifications fire continuously as long as data remains in the socket buffer.
* **Rationale**: LT is significantly less prone to starvation bugs, simpler to reason about when buffering partial RESP frames, and matches Redis's native event loop design.

### Q13: Why must all sockets registered with `epoll`/`kqueue` be non-blocking (`O_NONBLOCK`)?
**Answer:**
If a socket is blocking:
1. An `accept()` call could hang if a client sends a TCP SYN packet followed by an immediate RST before `accept()` completes.
2. A `read()` call expecting $N$ bytes could block if the client transmits fewer bytes than requested.
3. A `write()` call could block if the kernel socket send buffer fills up due to network congestion.
Because MiniRedis is single-threaded, any blocking system call freezes the entire database, preventing all other clients from being serviced.

### Q14: Walk me through a single iteration of your event loop.
**Answer:** Implemented in [`server/src/server.cpp`](file:///Users/pulkit/miniredis/server/src/server.cpp):
1. **Poll**: Call `poller_->poll(events, timeout_ms)` (delegates to `epoll_wait` or `kevent`).
2. **Dispatch Events**:
   * If `event.fd == listen_fd_`: Call `handle_accept()` in a loop to accept all pending incoming connections until `EAGAIN`.
   * If `event.fd == signal_pipe_[0]`: Break loop for graceful shutdown.
   * If `event.readable`: Call `handle_client_read(fd)` $\to$ read into buffer $\to$ parse commands $\to$ execute in store $\to$ stage reply.
   * If `event.writable`: Call `handle_client_write(fd)` $\to$ flush `client.out_buf` to socket.
3. **Cron Tasks**: Call `store_->run_active_expiration(25ms)` and update telemetry metrics.

### Q15: What happens when network backpressure occurs on a client connection?
**Answer:**
1. When serializing a reply, `handle_client_write(fd)` calls `::write(fd, buf, len)`.
2. If the client's TCP receive window is full or the network is congested, `::write()` writes only $M < len$ bytes and returns `EAGAIN`/`EWOULDBLOCK`.
3. The server leaves the remaining $len - M$ bytes in `client.out_buf`.
4. The server calls `poller_->modify(fd, PollEvent::READ | PollEvent::WRITE)`.
5. The server yields to the event loop. When the socket buffer drains and becomes writable again, the poller triggers a write event, and the remaining bytes are flushed.
6. Once `client.out_buf` is empty, `PollEvent::WRITE` is removed from the poller interest set to prevent busy-wait polling.

### Q16: What operations block your single-threaded event loop? How are they mitigated?
**Answer:**
1. **AOF `fsync()`**: Mitigated by running `FsyncPolicy::EVERYSEC` in a dedicated background worker thread (`aof_thread_`), keeping `fsync(fd)` off the event loop.
2. **`KEYS *` / Full keyspace scans**: Block the loop on large datasets ($O(N)$). Mitigated in production by using cursor-based `SCAN` commands.
3. **Deleting massive keys (e.g. multi-gigabyte collections)**: In real Redis, mitigated via `UNLINK` (lazy background deletion in separate thread).
4. **Virtual Memory Page Faults**: If memory is swapped to disk by the OS, execution stalls on memory access.

### Q17: How is graceful shutdown implemented?
**Answer:**
1. Signal handlers for `SIGINT` and `SIGTERM` catch termination signals.
2. The handler writes a single byte (`'q'`) to a self-pipe (`signal_pipe_[1]`).
3. The read end (`signal_pipe_[0]`) is registered with the poller.
4. When `poll()` returns with readable status on the self-pipe, `running_` is set to `false`.
5. The event loop terminates cleanly, flushes remaining AOF buffers to disk, invokes `::fsync()`, closes all open client descriptors, and joins background threads.

### Q18: What is the "self-pipe trick" and why is it necessary?
**Answer:** Signal handlers execute asynchronously in an interrupted stack frame. Only **async-signal-safe** functions (e.g., `write()`, `_exit()`) can be called inside a signal handler; calling `malloc()`, `printf()`, or interacting with C++ objects causes deadlocks and undefined behavior. The self-pipe trick writes a single byte to an anonymous pipe inside the signal handler. The read end of the pipe wakes up the standard `epoll`/`kqueue` multiplexer synchronously, allowing signal handling to execute safely within normal application flow.

### Q19: How do you prevent file descriptor leaks when clients disconnect?
**Answer:**
When `::read()` returns `0` (EOF) or an error (`ECONNRESET`, `EPIPE`), `Server::close_client(fd)` is invoked:
1. `poller_->del(fd)` unregisters the descriptor from kernel interest sets.
2. `::close(fd)` releases the OS file descriptor table slot.
3. `clients_.erase(fd)` destroys the `ClientSession` object, freeing `in_buf` and `out_buf` heap memory.

### Q20: What is the "thundering herd" problem and does it apply to MiniRedis?
**Answer:** The thundering herd problem occurs when multiple worker processes or threads block on `epoll_wait()` or `accept()` on the same shared listening socket. When a single client connects, the OS wakes all waiting threads, but only one succeeds while others receive `EAGAIN`. MiniRedis is single-threaded with a single listening socket, so thundering herd does not occur within the process. If multiple MiniRedis instances bind to the same port using `SO_REUSEPORT`, the Linux kernel handles connection distribution.

---

## Part 3: In-Memory Storage, Hash Tables & TTL (Q21 – Q28)

### Q21: How is the keyspace represented in memory in MiniRedis?
**Answer:**
* Primary storage: `std::unordered_map<std::string, Entry> data_` in [`server/include/miniredis/store.h`](file:///Users/pulkit/miniredis/server/include/miniredis/store.h).
* `Entry` struct contains:
  ```cpp
  struct Entry {
      std::string value;
      std::chrono::steady_clock::time_point expires_at;
      bool has_expiry;
  };
  ```
* Auxiliary expiration index: `std::vector<std::string> expiry_keys_` and `std::unordered_map<std::string, size_t> expiry_index_` to provide $O(1)$ random sampling for active expiration.

### Q22: What are the time and space complexities of `std::unordered_map` vs your custom `ChainedHashTable`?
**Answer:**
* **`std::unordered_map`**: Average $O(1)$ search/insert/delete, worst-case $O(N)$ when bucket collisions chain. It uses node-based separate chaining, allocating a separate heap node (`_Hash_node`) for every key-value entry (adding 16–24 bytes pointer overhead per entry and degrading CPU cache locality).
* **`ChainedHashTable`** ([`chained_hash_table.h`](file:///Users/pulkit/miniredis/server/include/miniredis/chained_hash_table.h)): Uses an array of bucket pointers sized to powers of two (`mask = capacity - 1`). It uses intrusive node lists or pooled allocations to reduce allocator overhead.
* **Space overhead**: `std::unordered_map` has ~24–32 bytes overhead per key in pointers plus 8 bytes per bucket pointer.

### Q23: How does hash collision resolution work in separate chaining, and what is worst-case lookup?
**Answer:** Each bucket holds a singly or doubly linked list of entries that hash to the same bucket index.
* **Resolution**: When a collision occurs, the new node is prepended to the bucket's list in $O(1)$ time.
* **Lookup**: Traverses the bucket's list comparing keys using equality.
* **Worst-case**: If all $N$ keys hash to the identical bucket (due to poor hash function or hash-flooding DoS), lookup degrades to $O(N)$. MiniRedis mitigates this using `std::hash` with uniform bit distributions.

### Q24: How is memory accounting implemented for keys and values?
**Answer:** Implemented in `Store::estimate_entry_memory()`:
$$\text{Memory} = \text{key.size()} + \text{val.size()} + \text{OVERHEAD\_PER\_ENTRY}$$
Where `OVERHEAD_PER_ENTRY = 64` bytes accounts for:
* `std::string` heap buffer overhead and small-string optimization (SSO) capacity.
* `std::unordered_map` bucket pointer (8 bytes) + linked node pointer (16 bytes).
* `Entry` struct fields (`std::chrono::steady_clock::time_point` + flags).
* LRU list node / LFU frequency node pointers.
Memory is updated atomically upon insert, update, delete, and eviction.

### Q25: What is the difference between passive (lazy) expiry and active expiry?
**Answer:**
* **Passive (Lazy) Expiry**: Whenever a key is accessed via `get()`, `set()`, or `del()`, `Store::check_and_expire(key)` checks if `entry.has_expiry && now >= entry.expires_at`. If expired, the key is deleted immediately and treated as non-existent.
* **Active Expiry**: Runs in the background of the event loop. Without active expiry, keys with TTL that are never accessed again would permanently leak memory.

### Q26: Why does active expiry use probabilistic sampling (20 keys, 25% threshold) instead of iterating the whole map?
**Answer:**
1. A full keyspace scan over 10 million keys would take hundreds of milliseconds, freezing the single-threaded event loop and violating latency SLAs.
2. **Algorithm**:
   * Sample 20 random keys with an active TTL from `expiry_keys_`.
   * Check and evict all expired keys among the 20.
   * If $> 25\%$ (i.e. $> 5$ keys) were expired, repeat the loop immediately.
3. **Statistical Guarantee**: Ensures that across time, the percentage of expired keys occupying memory never exceeds 25% of the total expiring keyspace, maintaining bounded memory without blocking the server.

### Q27: Why do you enforce a 25ms time budget on the active expiration loop?
**Answer:**
If millions of keys expire at the exact same second (e.g. `EXPIRE key 60` executed in a batch), the 25% threshold would trigger consecutive loops endlessly. A hard limit of **25ms per tick** ensures that the server relinquishes control back to the event loop, guaranteeing responsiveness for client I/O.

### Q28: How is the clock abstracted in tests to make TTL behavior deterministic?
**Answer:**
[`Store`](file:///Users/pulkit/miniredis/server/include/miniredis/store.h) accepts a configurable `ClockFn`:
```cpp
using ClockFn = std::function<std::chrono::steady_clock::time_point()>;
```
In production, it defaults to `std::chrono::steady_clock::now`. In GoogleTest suites, tests inject a mock lambda that returns an explicit timestamp variable. Tests advance time by incrementing the variable (`current_time += 15s`), verifying exact active and passive expirations without using `sleep()`.

---

## Part 4: Eviction Algorithms: LRU & LFU (Q29 – Q36)

### Q29: How does exact LRU eviction work and how do you achieve $O(1)$ operations?
**Answer:** Implemented in [`server/include/miniredis/lru_tracker.h`](file:///Users/pulkit/miniredis/server/include/miniredis/lru_tracker.h):
* **Data structures**:
  1. `std::list<std::string> lru_list_`: Doubly linked list tracking access order (head = most recently used, tail = least recently used).
  2. `std::unordered_map<std::string, std::list<std::string>::iterator> key_map_`: Maps key to its exact node iterator in the doubly linked list.
* **$O(1)$ Update (Access)**: Look up iterator in map $\to$ `lru_list_.splice(lru_list_.begin(), lru_list_, it)`.
* **$O(1)$ Eviction**: Read tail of list (`lru_list_.back()`) $\to$ erase from map and list $\to$ delete key from store.

### Q30: Why use `std::list::splice` instead of erasing and pushing to the list?
**Answer:** Calling `list.erase(it)` deallocates the linked list node from the heap, and `list.push_front(key)` allocates a new node on the heap. `std::list::splice()` simply unlinks the node's pointers (`prev` and `next`) and relinks them at the head. It executes in $O(1)$ time with **zero heap allocations and zero deallocations**, keeping the hot access path cache-friendly and fast.

### Q31: How does real Redis LRU differ from your exact LRU implementation?
**Answer:**
* **Real Redis**: Uses **approximated LRU**. It does not maintain a doubly linked list (which consumes 16–24 bytes per key in pointer overhead). Instead, every Redis object header contains a 24-bit timestamp (`lruclock`). On eviction, Redis samples $K$ random keys (default 5) and evicts the one with the oldest timestamp.
* **MiniRedis**: Implements **exact LRU**. It guarantees true mathematically optimal eviction order at the cost of additional pointer memory per key.

### Q32: How does exact LFU eviction work with frequency buckets?
**Answer:** Implemented in [`server/include/miniredis/lfu_tracker.h`](file:///Users/pulkit/miniredis/server/include/miniredis/lfu_tracker.h):
* **Frequency Buckets**: `std::unordered_map<uint64_t, std::list<std::string>> freq_buckets_`. Each frequency count points to a doubly linked list of keys sharing that frequency.
* **Node Map**: `key_nodes_` maps key to its `(frequency, list_iterator)`.
* **Min Frequency Pointer**: `uint64_t min_freq_` tracks the lowest non-empty frequency bucket.

### Q33: How do you maintain the minimum frequency pointer `min_freq_` in $O(1)$ time?
**Answer:**
* **On Insert**: The key starts with frequency 1. Thus, `min_freq_` is set to `1`.
* **On Access**:
  1. Key moves from frequency $F$ to $F + 1$.
  2. If bucket $F$ becomes empty and `min_freq_ == F`, increment `min_freq_ = F + 1`.
* **On Eviction**: Pop key from `freq_buckets_[min_freq_].front()`. Because `min_freq_` directly indexes the least frequently used bucket, finding the eviction candidate is $O(1)$ without searching.

### Q34: How does LFU break ties between keys with identical access counts?
**Answer:** Each frequency bucket in `freq_buckets_` is an ordered doubly linked list. When a key's frequency increments to $F$, it is pushed to the **back** of bucket $F$. When evicting, keys are popped from the **front** of bucket `min_freq_`. Thus, ties are broken via **FIFO (LRU within the same frequency)**.

### Q35: What are the trade-offs between LRU and LFU cache policies?
**Answer:**
* **LRU**:
  * *Strengths*: Adapts instantly to shifting working sets; simple to implement.
  * *Weakness*: Vulnerable to **scan pollution**. A single sequential read of many keys displaces the entire working set from cache.
* **LFU**:
  * *Strengths*: Frequency-aware; protects hot keys against transient scan bursts.
  * *Weakness*: **Frequency starvation**. Historical hot keys that are no longer accessed remain in cache forever with high frequency scores unless decayed over time.

### Q36: What is the Morris counter and logarithmic frequency decay in real Redis?
**Answer:** Real Redis implements approximated LFU inside a single 24-bit field:
* 16 bits: Last decay time (`ldt`).
* 8 bits: Logarithmic access counter (`counter`, 0–255).
The counter does not increment on every hit; it increments with probability $1 / (\text{counter} \times \text{lfu\_log\_factor} + 1)$ (Morris counter), allowing an 8-bit byte to represent millions of accesses. Before incrementing, the counter decays based on elapsed time since `ldt`.

---

## Part 5: Persistence, AOF & Crash Recovery (Q37–Q43)

### Q37: What is an Append-Only File (AOF) vs snapshotting (RDB)?
**Answer:**
* **RDB (Redis Database Snapshot)**: Point-in-time binary snapshot of memory dumped to disk at configured intervals (e.g., every 5 minutes). High write performance, compact backup, but risks losing all data written since the last snapshot.
* **AOF (Append-Only File)**: Append-only log recording every write command (`SET`, `DEL`, `EXPIRE`) in RESP format. Offers higher durability (loss bounded to $\le 1$ second under `everysec`), but produces larger file sizes.

### Q38: What are the three `fsync` policies (`always`, `everysec`, `no`) and their trade-offs?
**Answer:**
| Policy | Throughput | Durability Guarantee | Latency Impact |
| :--- | :--- | :--- | :--- |
| `always` | ~1,000 ops/sec | Zero data loss; fsync blocks every write | High (disk I/O on every command) |
| `everysec` | ~100,000 ops/sec | Max 1-2 seconds of data lost on crash | Negligible (background thread) |
| `no` | >150,000 ops/sec | OS decides flush timing (up to 30s loss) | None (delegated to kernel page cache) |

### Q39: How does background `fsync` work in MiniRedis without blocking the event loop?
**Answer:** Implemented in [`server/src/aof.cpp`](file:///Users/pulkit/miniredis/server/src/aof.cpp):
* When configured with `FsyncPolicy::EVERYSEC`, a background thread (`aof_thread_`) runs an atomic loop.
* Every 1,000 ms, the thread executes `::fsync(fd_)`.
* Mutexes guard file descriptor access during rewrite rotation, but mutating commands on the main thread only call `::write()`, which writes directly into kernel page cache in microseconds.

### Q40: How does crash recovery handle a partially written or corrupted AOF record?
**Answer:**
1. If the server crashes or loses power mid-write, the final AOF line may contain a truncated RESP command (e.g. `*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nba`).
2. During restart, `AofManager::load_and_replay()` reads the file sequentially using `RespParser`.
3. If the final command returns `ParseResult::INCOMPLETE`, MiniRedis uses `::truncate()` / `ftruncate(fd, valid_bytes)` to cleanly strip the partial record and resumes loading without data corruption.

### Q41: How does AOF compaction / rewrite work and why is POSIX `rename()` critical?
**Answer:**
* **Compaction**: The raw AOF accumulates historical updates (`SET k 1`, `SET k 2`, `DEL k`). Compaction iterates `data_` and writes only the current state of live keys to a temporary file (`miniredis.aof.tmp`).
* **Atomic Swap**: Once written, the temporary file is swapped using `::rename("miniredis.aof.tmp", "miniredis.aof")`.
* **POSIX Guarantee**: In POSIX filesystems, `rename()` is an **atomic system call**. If the machine crashes during the rewrite, the original AOF remains completely intact without corruption.

### Q42: Why are TTL expiration timestamps logged as absolute timestamps (`PEXPIREAT`) in AOF rather than relative durations?
**Answer:**
If a client executes `SET foo bar EX 10` at 12:00:00, the key expires at 12:00:10. If the server crashes at 12:00:08 and restarts at 12:01:00, replaying a relative `EX 10` would incorrectly reset the expiration to 12:01:10 (extending life by 70 seconds). By logging the absolute epoch timestamp (`PEXPIREAT foo 1727611210000`), the key expires correctly upon recovery.

### Q43: How does copy-on-write (`fork()`) work during real Redis background rewrites?
**Answer:** Real Redis executes `BGREWRITEAOF` by calling `fork()`. The OS creates a child process sharing the parent's memory pages marked read-only.
* While the child iterates memory and streams the snapshot to disk, the parent continues serving writes.
* When the parent modifies a page, the OS kernel triggers a Copy-on-Write (CoW) page fault, duplicating only that 4KB page. This yields a non-blocking snapshot without requiring locks or in-memory duplication.

---

## Part 6: Full Stack Telemetry, Bridge & UI (Q44 – Q47)

### Q44: Why build a Node.js bridge instead of having the browser connect directly to MiniRedis via WebSockets?
**Answer:**
1. **Protocol Mismatch**: Browsers cannot open raw TCP sockets; they can only establish HTTP or WebSocket connections (which require a WS handshake and masked frame encoding).
2. **Separation of Concerns**: MiniRedis's C++ core remains pure and hyper-optimized for the RESP wire protocol.
3. **Telemetry Fan-Out**: The Node.js bridge maintains a single persistent RESP connection to MiniRedis, polls `INFO` and `EVENTS`, and broadcasts updates to multiple connected browser dashboards over WebSocket.

### Q45: How does the lock-free eviction/expiration ring buffer communicate events without allocating memory?
**Answer:** Implemented in [`server/include/miniredis/eviction_ring_buffer.h`](file:///Users/pulkit/miniredis/server/include/miniredis/eviction_ring_buffer.h):
* Uses a fixed-size power-of-two array (`EvictionEvent buffer_[1024]`).
* Head and tail indices are managed via unsigned atomic integers (`std::atomic<uint64_t>`).
* Ring buffer slots store fixed-size stack structs with truncated `char key[64]` buffers.
* Event recording never invokes `malloc()` or `new` on the critical eviction path.

### Q46: How does the React dashboard handle high-frequency WebSocket metric bursts (100k ops/sec) without UI freezes?
**Answer:** Implemented in [`dashboard/src/App.jsx`](file:///Users/pulkit/miniredis/dashboard/src/App.jsx):
1. **Decoupled Buffer**: Incoming WebSocket messages are appended to a transient ref buffer (`pendingEventsRef`) without triggering React state updates.
2. **RAF Throttling**: State updates are scheduled via `requestAnimationFrame()` or batched on a 100ms ticker.
3. **Fixed Window Slicing**: Time-series charts maintain a sliding window of max 60 data points (`slice(-60)`), bounding SVG DOM complexity.

### Q47: How was CSS Grid containment used to prevent UI layout thrashing during stress tests?
**Answer:** Under heavy eviction bursts, keyspace tables and SVG charts generated text wider than default grid tracks. Standard CSS Grid elements have default `min-width: auto`, which causes grid tracks to expand based on `min-content`, pushing panels off-screen. Adding `min-width: 0; overflow: hidden;` to CSS Grid child panels forces the layout engine to contain overflowing elements within their allocated fractional column tracks (`1fr`).

---

## Part 7: Concurrency, Scaling & Advanced Architectures (Q48 – Q50)

### Q48: If you wanted to make MiniRedis multi-threaded, what architecture would you choose?
**Answer:**
* **Option A: Redis 6.0 I/O Threading Model (Recommended)**: The main event loop remains single-threaded for command execution, but delegates socket reading, RESP parsing, and socket reply writing to a pool of worker threads. This relieves network I/O bottlenecks without adding locks to the storage engine.
* **Option B: Partitioned Shared-Nothing / Thread-per-Core Model (KeyDB / Seastar)**: Partition the keyspace across $N$ threads using consistent hashing (`thread_id = hash(key) % N`). Each thread runs an independent event loop and owns an isolated store segment, eliminating lock contention.

### Q49: How would you implement master-replica replication and horizontal sharding?
**Answer:**
* **Replication**:
  1. Replica connects and sends `SYNC`.
  2. Master triggers an AOF/snapshot rewrite and streams the initial state to the replica.
  3. Master streams all mutating write commands downstream via an in-memory replication backlog buffer.
* **Horizontal Sharding (Redis Cluster)**:
  1. Partition keyspace into 16,384 virtual hash slots: `slot = CRC16(key) mod 16384`.
  2. Assign slot ranges to different master nodes.
  3. Clients route queries using a slot-to-node map. If a key belongs to a different node, the server responds with a `-MOVED <slot> <ip>:<port>` redirect.

### Q50: How would you design a Sorted Set (ZSET) using a Skip List and Hash Map?
**Answer:**
A Redis ZSET combines two data structures:
1. `std::unordered_map<std::string, double> dict_`: Maps `member` $\to$ `score` in $O(1)$ time for fast lookups.
2. **Skip List (`zskiplist`)**: A probabilistic multi-level linked list maintaining members sorted by `(score, member)`:
   * Provides $O(\log N)$ average insertion, deletion, and search.
   * Forward pointers with span counts allow finding the rank of any member in $O(\log N)$ time.
   * Range queries (`ZRANGEBYSCORE min max`) locate the starting node in $O(\log N)$ and iterate forward sequentially in $O(M)$ time, outperforming balanced trees (AVL/Red-Black) for concurrent memory layout and range scans.
