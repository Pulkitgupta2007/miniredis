# MiniRedis

An in-memory, single-threaded, event-driven key-value store built with modern C++17, implementing the Redis RESP2 protocol, complete with a Node.js monitoring bridge and a real-time React telemetry dashboard.

---

## Project Status & Phases

- [x] **Phase 1: In-Memory Key-Value Core & Storage Engine** (Complete: 26/26 unit tests passing)
- [x] **Phase 2: Networking + RESP2 Engine** (Complete: 36/36 unit tests passing + real `redis-cli` integration tests)
- [x] **Phase 3: TTL / Expiry System** (Complete: 39/39 unit tests passing + zero-sleep mock clock tests)
- [x] **Phase 4: Eviction Engine (LRU and LFU)** (Complete: 44/44 unit tests passing + property-based randomized verification)
- [x] **Phase 5: Persistence (AOF Engine)** (Complete: 49/49 unit tests passing + crash truncation recovery & atomic rewrite)
- [x] **Phase 6: Observability (INFO & Telemetry)** (Complete: 55/55 unit tests passing + EVENTS stream)
- [x] **Phase 7: Node.js Bridge & Real-Time React Dashboard** (Complete: 13/13 bridge tests + 9/9 React component tests)
- [ ] **Phase 8: Benchmarks, Sanitizers & Docker / GitHub Actions CI**

---

## Quickstart

### Prerequisites
- Modern C++17 compiler (`clang++` or `g++`)
- CMake $\ge 3.14$
- Node.js $\ge 18$ & npm
- Python 3 (for integration tests)
- `redis-cli` (optional, for manual interactive CLI testing)

### 1. Build and Run MiniRedis Server
```bash
cd server
mkdir -p build && cd build
cmake ..
cmake --build .
./miniredis-server 6379 127.0.0.1
```

### 2. Start Node.js Bridge
```bash
cd bridge
npm install
npm start
# Bridge starts on http://127.0.0.1:4000 (WebSocket proxy ready)
```

### 3. Start React Dashboard
```bash
cd dashboard
npm install
npm run dev
# Open http://localhost:3000 in your browser
```

---

## Running Test Suites

```bash
# 1. C++ GoogleTest Suite (55 unit tests)
cd server/build
ctest --output-on-failure

# 2. Python Integration Suite (Real redis-cli + raw TCP sockets)
python3 ../tests/integration_test.py

# 3. Node.js Bridge Unit & E2E Tests (13 tests with mock TCP + real server)
cd bridge
npm test

# 4. React Dashboard Component Tests (9 Vitest component tests)
cd dashboard
npm test
```

---

## Documentation
- [System Architecture](docs/DESIGN.md)
- [Engineering Trade-Offs](docs/TRADEOFFS.md)
