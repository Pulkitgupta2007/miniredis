# MiniRedis

An in-memory, single-threaded, event-driven key-value store built with modern C++17, implementing the Redis RESP2 protocol, complete with a Node.js monitoring bridge and a real-time React telemetry dashboard.

---

## Project Status & Phases

- [x] **Phase 1: In-Memory Key-Value Core & TTL Engine** (Complete: 22/22 unit tests passing)
- [x] **Phase 2: Networking + RESP2 Engine** (Complete: 32/32 unit tests passing + real `redis-cli` integration tests)
- [ ] **Phase 3: Additional Data Structures & Memory Eviction**
- [ ] **Phase 4: Persistence (AOF / RDB Snapshotting)**
- [ ] **Phase 5: Node.js Bridge & Real-Time React Dashboard**
- [ ] **Phase 6: Benchmarks, Sanitizers & Docker / GitHub Actions CI**

---

## Quickstart

### Prerequisites
- Modern C++17 compiler (`clang++` or `g++`)
- CMake $\ge 3.14$
- Python 3 (for integration tests)
- `redis-cli` (optional, for manual interactive CLI testing)

### Build Instructions
```bash
cd server
mkdir -p build && cd build
cmake ..
cmake --build .
```

### Running the Server
```bash
cd server/build
./miniredis-server 6379 127.0.0.1
```

### Running Tests
```bash
# 1. GoogleTest Unit Tests (Store + RESP Parser)
cd server/build
ctest --output-on-failure

# 2. Integration Test (Runs real redis-cli + raw TCP socket pipelines)
python3 ../tests/integration_test.py
```

---

## Documentation
- [System Architecture](docs/DESIGN.md)
- [Engineering Trade-Offs](docs/TRADEOFFS.md)
