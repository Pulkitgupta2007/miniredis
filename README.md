# MiniRedis

An in-memory, single-threaded, event-driven key-value store built in modern C++17, implementing the Redis RESP2 protocol, paired with an append-only file persistence engine, an exact $\mathcal{O}(1)$ LRU/LFU cache eviction engine, a Node.js monitoring bridge, and a real-time React telemetry dashboard.

Designed from first principles to demonstrate systems engineering rigor, network socket multiplexing, zero-copy protocol parsing, deterministic cache algorithms, memory accounting, and production observability.

---

## Architecture Overview

```mermaid
flowchart TD
    Client["Clients (redis-cli / redis-benchmark)"] -->|TCP Socket| Reactor["Networking Reactor (epoll / kqueue)"]
    Reactor -->|Raw Byte Stream| Parser["RESP2 Parser & Tokenizer (std::string_view)"]
    Parser -->|Command Tokens| Dispatcher["Command Dispatcher & Router"]

    Dispatcher -->|Write Mutate| Store["Primary Storage Engine (std::unordered_map)"]
    Dispatcher -->|Read Lookup| Store
    Dispatcher -->|Eviction Policy Check| EvictionEngine["Eviction Engine (maxmemory)"]
    
    EvictionEngine -->|Track Access| LRU["Exact O(1) LRU (Sentinel DLL + Map)"]
    EvictionEngine -->|Track Frequency| LFU["Exact O(1) LFU (Frequency Buckets + Map)"]
    EvictionEngine -->|Record Event| RingBuffer["Telemetry Ring Buffer (Events)"]

    Dispatcher -->|Mutating Command| AOF["AOF Persistence Engine"]
    AOF -->|append| AOFFile["Append-Only File (.aof)"]
    AOFWorker["Background Fsync Thread (1 Hz)"] -->|fsync| AOFFile

    Dispatcher -->|Passive Expire| ExpiryEngine["TTL Expiration Engine"]
    ExpiryEngine -->|O(1) Uniform Sampling| Store

    Bridge["Node.js WebSocket Bridge (Port 4000)"] -->|TCP Polling INFO / EVENTS| Reactor
    Bridge -->|WebSocket Broadcast| Dashboard["React Live Telemetry Dashboard (Port 3000)"]
    Dashboard -->|Execute Commands| Bridge
```

---

## Supported Commands

| Category | Command | Syntax | Description | Complexity |
|---|---|---|---|---|
| **Core Key-Value** | `SET` | `SET key value [EX s \| PX ms] [NX \| XX]` | Sets key to value with optional TTL and existence constraints | $\mathcal{O}(1)$ |
| | `GET` | `GET key` | Returns the value associated with key, or nil if missing/expired | $\mathcal{O}(1)$ |
| | `DEL` | `DEL key [key ...]` | Removes specified keys; returns count of keys removed | $\mathcal{O}(N)$ where $N$ = keys |
| | `EXISTS` | `EXISTS key [key ...]` | Returns count of keys that exist and are unexpired | $\mathcal{O}(N)$ where $N$ = keys |
| | `KEYS` | `KEYS pattern` | Returns all keys matching a glob-style wildcard pattern | $\mathcal{O}(N)$ where $N$ = total keys |
| | `DBSIZE` | `DBSIZE` | Returns total count of active, unexpired keys | $\mathcal{O}(1)$ |
| | `FLUSHALL` | `FLUSHALL` | Deletes all keys from the database | $\mathcal{O}(N)$ |
| **Expiration & TTL** | `EXPIRE` | `EXPIRE key seconds` | Sets key expiration timeout in relative seconds | $\mathcal{O}(1)$ |
| | `PEXPIRE` | `PEXPIRE key milliseconds` | Sets key expiration timeout in relative milliseconds | $\mathcal{O}(1)$ |
| | `TTL` | `TTL key` | Returns remaining time-to-live in seconds (-1 no TTL, -2 missing) | $\mathcal{O}(1)$ |
| | `PTTL` | `PTTL key` | Returns remaining time-to-live in milliseconds | $\mathcal{O}(1)$ |
| | `PERSIST` | `PERSIST key` | Removes TTL expiration, making key persistent | $\mathcal{O}(1)$ |
| **Memory & Eviction** | `CONFIG SET` | `CONFIG SET parameter value` | Sets `maxmemory` or `maxmemory-policy` at runtime | $\mathcal{O}(1)$ |
| | `CONFIG GET` | `CONFIG GET parameter` | Retrieves current configuration values | $\mathcal{O}(1)$ |
| | `MEMORY USAGE` | `MEMORY USAGE key` | Returns estimated bytes occupied by key and value | $\mathcal{O}(1)$ |
| **Observability** | `INFO` | `INFO` | Emits formatted server, memory, clients, and cache hit metrics | $\mathcal{O}(1)$ |
| | `EVENTS` | `EVENTS [limit]` | Streams recent eviction and expiration events from ring buffer | $\mathcal{O}(M)$ where $M$ = limit |
| | `LRUKEYS` | `LRUKEYS [limit]` | Returns keys in current MRU-to-LRU doubly linked list order | $\mathcal{O}(M)$ where $M$ = limit |
| **Connection** | `PING` | `PING [message]` | Tests server liveness; returns PONG or echo message | $\mathcal{O}(1)$ |
| | `QUIT` | `QUIT` | Gracefully closes the client connection | $\mathcal{O}(1)$ |

---

## Quickstart

### Prerequisites
- Modern C++17 compiler (`clang++` or `g++`)
- CMake $\ge 3.14$
- Node.js $\ge 18$ & npm
- Python 3 (for integration test suite)
- Docker & Docker Compose (optional, for containerized run)

---

### Option A: Run with Docker Compose (Recommended)

Start the entire distributed stack (C++ MiniRedis Server, Node.js Bridge, and React Dashboard) with one command:

```bash
docker compose up --build
```

- **MiniRedis Server**: Port `6379` (raw RESP TCP)
- **Node.js Bridge**: Port `4000` (WebSocket proxy & HTTP)
- **React Dashboard**: Open [http://localhost:3000](http://localhost:3000) in your browser

To stop the containers:
```bash
docker compose down
```

---

### Option B: Build and Run Natively

#### 1. Compile & Launch MiniRedis Server
```bash
cd server
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j
./miniredis-server 6379 127.0.0.1
```

#### 2. Start the Node.js Telemetry Bridge
```bash
cd bridge
npm install
npm start
# Bridge connects to MiniRedis on port 6379 and starts WebSocket proxy on port 4000
```

#### 3. Start the React Live Dashboard
```bash
cd dashboard
npm install
npm run dev
# Dashboard launches on http://localhost:3000 with real-time graphs and console
```

---

## Testing & Verification

MiniRedis contains a multi-tiered test suite ensuring protocol correctness, memory safety, and concurrency resilience:

```bash
# 1. C++ GoogleTest Suite (59 comprehensive unit tests)
cd server/build
ctest --output-on-failure

# 2. Python Integration Suite (End-to-end tests with real redis-cli)
python3 tests/integration_test.py

# 3. Node.js Bridge Tests (Mock TCP server + RESP framing tests)
cd bridge
npm test

# 4. React Dashboard Component Tests (Vitest + Testing Library)
cd dashboard
npm test

# 5. High-Concurrency Stress Test (120 concurrent clients, 50,000+ ops/sec)
python3 tests/stress_test.py
```

### LLVM Sanitizer Verification
MiniRedis is built and validated 100% clean under AddressSanitizer, UndefinedBehaviorSanitizer, and ThreadSanitizer:

```bash
# AddressSanitizer & UndefinedBehaviorSanitizer build
cd server
mkdir -p build_asan && cd build_asan
cmake -DENABLE_ASAN=ON .. && cmake --build . -j
ctest --output-on-failure

# ThreadSanitizer build (verifies AOF background fsync worker thread)
cd server
mkdir -p build_tsan && cd build_tsan
cmake -DENABLE_TSAN=ON .. && cmake --build . -j
ctest --output-on-failure
```

---

## Performance Benchmarks vs. Real Redis

MiniRedis was benchmarked side-by-side against official production Redis using `redis-benchmark` on identical Apple Silicon hardware (50 parallel connections, loopback TCP):

| Workload | Metric | Real Redis | MiniRedis | Throughput Ratio |
|---|---|---|---|---|
| **SET (Non-Pipelined, -P 1)** | Throughput | 161,290 ops/s | 90,909 ops/s | **56.4%** |
| | P50 Latency | 0.143 ms | 0.527 ms | - |
| | P99 Latency | 1.263 ms | 1.071 ms | - |
| **GET (Non-Pipelined, -P 1)** | Throughput | 156,250 ops/s | 111,607 ops/s | **71.4%** |
| | P50 Latency | 0.159 ms | 0.431 ms | - |
| | P99 Latency | 0.495 ms | 0.871 ms | - |
| **SET (Pipelined, -P 16)** | Throughput | 1,470,588 ops/s | 161,290 ops/s | **11.0%** |
| **GET (Pipelined, -P 16)** | Throughput | 2,083,333 ops/s | 204,918 ops/s | **9.8%** |

Run the automated benchmark suite locally:
```bash
python3 bench/benchmark_comparison.py
```
For a detailed technical analysis of where and why MiniRedis diverges under pipelining (allocator churn, buffer shifting, and cache locality), read [bench/BENCHMARKS.md](file:///Users/pulkit/miniredis/bench/BENCHMARKS.md).

---

## Live Telemetry Dashboard Preview

```
+-------------------------------------------------------------------------------+
|  MINIREDIS TELEMETRY DASHBOARD                          [Status: CONNECTED]   |
+-------------------------------------------------------------------------------+
|  Used Memory: 1.42 MB   |  Total Keys: 4,096  |  Hit Rate: 98.4%              |
|  Ops / Sec:   48,210    |  Evictions:  128    |  Expirations: 512             |
+-------------------------------------------------------------------------------+
|  [ Live SVG Memory & Throughput Timeline Graph ]                              |
|   1.5MB |       /\                                                            |
|   1.0MB |  ____/  \__/\__                                                     |
|   0.5MB | /              \________                                            |
+-------------------------------------------------------------------------------+
|  [ LRU Cache Order Visualizer (MRU -> LRU) ]                                  |
|  [HEAD] session:usr101 <-> token:auth99 <-> cart:u101 ... <-> cache:old [TAIL]|
+-------------------------------------------------------------------------------+
|  [ Live Event Feed ]               |  [ Interactive Command Console ]         |
|  - EVICTION: key 'cache:old' freed |  > SET session:usr101 "active" EX 300    |
|  - EXPIRY:   key 'token:temp'      |  OK                                      |
+-------------------------------------------------------------------------------+
```

---

## Known Limitations & Deliberate Trade-Offs

To maintain systems programming depth over superficial breadth, several production Redis features were deliberately excluded as documented in [docs/TRADEOFFS.md](file:///Users/pulkit/miniredis/docs/TRADEOFFS.md):

1. **Strings Only**: MiniRedis focuses exclusively on String key-value pairs (`SET`, `GET`, `DEL`) rather than composite types (Hashes, Lists, Sets, Sorted Sets, Streams).
2. **Single-Threaded Reactor**: Uses a pure single-threaded event loop (`epoll`/`kqueue`). Redis 6 multi-threaded socket I/O was excluded to maintain a lock-free architecture.
3. **AOF Persistence over RDB Snapshots**: Employs continuous append-only logging with atomic compactions instead of `fork()`-based copy-on-write RDB snapshots to prevent COW memory bloat.
4. **Single-Node Architecture**: Clustering (`CRC16` slot partitioning) and master-replica replication (`PSYNC`) are excluded in favor of single-node storage and eviction mechanics.

---

## Documentation Index

- [System Design & Complexity Matrix](file:///Users/pulkit/miniredis/docs/DESIGN.md): Detailed algorithmic breakdown of every subsystem, data structures, and asymptotic bounds.
- [Engineering Trade-Offs & Non-Goals](file:///Users/pulkit/miniredis/docs/TRADEOFFS.md): Rationale behind storage layouts, eviction designs, durability modes, and benchmark divergences.
- [Systems Technical Interview Guide](file:///Users/pulkit/miniredis/docs/INTERVIEW.md): 20 technical questions and answers covering event loops, RESP framing, memory management, and sanitizers.
- [Comparative Benchmark Analysis](file:///Users/pulkit/miniredis/bench/BENCHMARKS.md): Side-by-side performance profiling against official Redis.
