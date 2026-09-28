# MiniRedis vs. Real Redis Benchmark Results

**Environment**: Darwin arm64 (Apple Silicon), loopback network (`127.0.0.1`), 50 parallel client connections.

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

---

## Where and Why MiniRedis is Slower (Honest Architectural Analysis)

### 1. Competitive in Non-Pipelined RTT-Bound Workloads (56% – 71% of Redis)
In non-pipelined mode (`-P 1`), every single operation requires a round-trip network transit (Client $\to$ Loopback $\to$ Server $\to$ Loopback $\to$ Client). Because network syscall overhead (`read`, `write`, `epoll`/`kqueue`) dominates the ~100ns in-memory hash table lookup, MiniRedis achieves over 90,000–111,000 ops/sec and sub-millisecond P99 latency.

### 2. The Pipelining Divergence (10% – 11% of Redis)
When pipelining is enabled (`-P 16`), network round trips are amortized across 16 commands per TCP frame. At this point, the bottleneck transitions completely from network I/O to pure CPU execution and memory bandwidth. Real Redis hits 1.4M–2.0M ops/sec, while MiniRedis tops out at ~160k–205k ops/sec due to four fundamental design trade-offs:

1. **Heap Allocation Churn on Hot Path (`malloc`/`free`)**:
   - **Real Redis**: Employs static pre-allocated buffer slices for all standard protocol replies (`shared.ok`, `shared.pong`, `shared.colon`, `shared.nullbulk`). Output data is appended directly into a fixed 16 KB static client buffer (`c->buf`), resulting in zero heap allocations for basic SET/GET commands.
   - **MiniRedis**: Constructs heap-allocated `std::string` objects for command arguments (`std::vector<std::string> args`), parsed tokens, and reply strings. Under 2,000,000 ops/sec, the memory allocator (`malloc`/`free`) becomes the primary CPU bottleneck.
2. **Buffer Compaction Overhead (`std::string::erase`)**:
   - **Real Redis**: Maintains a read cursor offset pointer (`c->qb_pos`) into its query buffer, advancing the pointer in $\mathcal{O}(1)$ without moving memory.
   - **MiniRedis**: Calls `client.in_buf.erase(0, bytes_consumed)` after every parsed command. Within a 16-command pipeline, this shifts the remaining buffer contents in memory 16 consecutive times via `memmove()`.
3. **Data Structure Cache Locality (`std::unordered_map` vs. `dict`)**:
   - **Real Redis**: Uses custom C `dict` with contiguous pointer tables tuned for 64-byte CPU cache lines with `jemalloc`.
   - **MiniRedis**: Uses `std::unordered_map` where each key-value entry is wrapped in an individual node (`std::__hash_node`) allocated separately on the heap, resulting in pointer-chasing CPU cache misses.
4. **LRU / LFU Bookkeeping Overhead on Reads**:
   - **Real Redis**: Does not maintain a linked list. Reads simply write a 24-bit clock integer inside the object header (`robj::lru = LRU_CLOCK()`).
   - **MiniRedis**: Fully updates both an explicit doubly-linked list (`lru_tracker_.touch()`) and frequency bucket pointers (`lfu_tracker_.touch()`) on every single `GET`, modifying multiple pointers and dirtying cache lines on what should be read-only lookups.

