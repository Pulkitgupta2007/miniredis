# MiniRedis

An in-memory, single-threaded, event-driven key-value store built with modern C++17, implementing the Redis RESP2 protocol, complete with a Node.js monitoring bridge and a real-time React telemetry dashboard.

---

## Project Status & Phases

- [x] **Phase 1: In-Memory Key-Value Core & TTL Engine** (Complete: 14/14 unit tests passing)
- [ ] **Phase 2: RESP2 Protocol Streaming Parser & Serializer**
- [ ] **Phase 3: Non-Blocking Event-Driven Network Engine (`epoll` / `kqueue`)**
- [ ] **Phase 4: Command Dispatcher & `redis-cli` Compatibility**
- [ ] **Phase 5: Advanced Data Structures & Memory Eviction**
- [ ] **Phase 6: Node.js Bridge & Real-Time React Dashboard**
- [ ] **Phase 7: Benchmarks, Sanitizers & Docker / GitHub Actions CI**

---

## Phase 1 Quickstart

### Prerequisites
- Modern C++17 compiler (`clang++` $\ge 10$ or `g++` $\ge 9$)
- CMake $\ge 3.14$
- POSIX-compliant OS (Linux / Ubuntu / WSL / macOS)

### Build Instructions
```bash
cd server
mkdir -p build && cd build
cmake ..
cmake --build .
```

### Running Tests
```bash
cd server/build
./store_test
# Or via CTest
ctest --output-on-failure
```

---

## Documentation
- [System Architecture](docs/DESIGN.md)
- [Engineering Trade-Offs](docs/TRADEOFFS.md)
