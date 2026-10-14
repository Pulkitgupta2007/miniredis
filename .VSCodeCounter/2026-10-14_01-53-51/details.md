# Details

Date : 2026-10-14 01:53:51

Directory /Users/pulkit/Desktop/miniredis

Total : 53 files,  11656 codes, 183 comments, 1490 blanks, all 13329 lines

[Summary](results.md) / Details / [Diff Summary](diff.md) / [Diff Details](diff-details.md)

## Files
| filename | language | code | comment | blank | total |
| :--- | :--- | ---: | ---: | ---: | ---: |
| [README.md](/README.md) | Markdown | 179 | 0 | 54 | 233 |
| [bridge/Dockerfile](/bridge/Dockerfile) | Docker | 10 | 0 | 7 | 17 |
| [bridge/package-lock.json](/bridge/package-lock.json) | JSON | 37 | 0 | 1 | 38 |
| [bridge/package.json](/bridge/package.json) | JSON | 12 | 0 | 1 | 13 |
| [bridge/src/parser.js](/bridge/src/parser.js) | JavaScript | 133 | 27 | 19 | 179 |
| [bridge/src/redis-client.js](/bridge/src/redis-client.js) | JavaScript | 183 | 7 | 33 | 223 |
| [bridge/src/server.js](/bridge/src/server.js) | JavaScript | 259 | 9 | 30 | 298 |
| [ci/github-ci.yml](/ci/github-ci.yml) | YAML | 89 | 0 | 20 | 109 |
| [dashboard/Dockerfile](/dashboard/Dockerfile) | Docker | 12 | 2 | 10 | 24 |
| [dashboard/index.html](/dashboard/index.html) | HTML | 13 | 0 | 1 | 14 |
| [dashboard/package-lock.json](/dashboard/package-lock.json) | JSON | 3,072 | 0 | 1 | 3,073 |
| [dashboard/package.json](/dashboard/package.json) | JSON | 25 | 0 | 1 | 26 |
| [dashboard/src/App.css](/dashboard/src/App.css) | PostCSS | 1,652 | 22 | 265 | 1,939 |
| [dashboard/src/App.jsx](/dashboard/src/App.jsx) | JavaScript JSX | 177 | 7 | 25 | 209 |
| [dashboard/src/components/CommandConsole.jsx](/dashboard/src/components/CommandConsole.jsx) | JavaScript JSX | 274 | 3 | 29 | 306 |
| [dashboard/src/components/EventFeed.jsx](/dashboard/src/components/EventFeed.jsx) | JavaScript JSX | 68 | 0 | 6 | 74 |
| [dashboard/src/components/Header.jsx](/dashboard/src/components/Header.jsx) | JavaScript JSX | 36 | 0 | 4 | 40 |
| [dashboard/src/components/KeyspaceExplorer.jsx](/dashboard/src/components/KeyspaceExplorer.jsx) | JavaScript JSX | 461 | 10 | 39 | 510 |
| [dashboard/src/components/LruVisualizer.jsx](/dashboard/src/components/LruVisualizer.jsx) | JavaScript JSX | 170 | 3 | 13 | 186 |
| [dashboard/src/components/MemoryOpsChart.jsx](/dashboard/src/components/MemoryOpsChart.jsx) | JavaScript JSX | 248 | 8 | 28 | 284 |
| [dashboard/src/components/MetricCards.jsx](/dashboard/src/components/MetricCards.jsx) | JavaScript JSX | 69 | 3 | 6 | 78 |
| [dashboard/src/components/TrafficPlayground.jsx](/dashboard/src/components/TrafficPlayground.jsx) | JavaScript JSX | 351 | 7 | 34 | 392 |
| [dashboard/src/main.jsx](/dashboard/src/main.jsx) | JavaScript JSX | 8 | 0 | 2 | 10 |
| [dashboard/vite.config.js](/dashboard/vite.config.js) | JavaScript | 12 | 0 | 2 | 14 |
| [docker-compose.yml](/docker-compose.yml) | YAML | 33 | 0 | 3 | 36 |
| [docs/BUG\_STORIES.md](/docs/BUG_STORIES.md) | Markdown | 192 | 0 | 37 | 229 |
| [docs/CHEATSHEET.md](/docs/CHEATSHEET.md) | Markdown | 67 | 0 | 24 | 91 |
| [docs/DESIGN.md](/docs/DESIGN.md) | Markdown | 251 | 0 | 51 | 302 |
| [docs/INTERVIEW.md](/docs/INTERVIEW.md) | Markdown | 91 | 0 | 34 | 125 |
| [docs/INTERVIEW\_PREP.md](/docs/INTERVIEW_PREP.md) | Markdown | 159 | 0 | 26 | 185 |
| [docs/INTERVIEW\_QA.md](/docs/INTERVIEW_QA.md) | Markdown | 295 | 0 | 67 | 362 |
| [docs/PROTOCOL.md](/docs/PROTOCOL.md) | Markdown | 50 | 0 | 13 | 63 |
| [docs/TRADEOFFS.md](/docs/TRADEOFFS.md) | Markdown | 159 | 0 | 53 | 212 |
| [server/CMakeLists.txt](/server/CMakeLists.txt) | CMake | 42 | 0 | 10 | 52 |
| [server/Dockerfile](/server/Dockerfile) | Docker | 20 | 2 | 9 | 31 |
| [server/include/miniredis/aof.h](/server/include/miniredis/aof.h) | C++ | 56 | 4 | 19 | 79 |
| [server/include/miniredis/chained\_hash\_table.h](/server/include/miniredis/chained_hash_table.h) | C++ | 117 | 4 | 23 | 144 |
| [server/include/miniredis/command.h](/server/include/miniredis/command.h) | C++ | 16 | 0 | 7 | 23 |
| [server/include/miniredis/eviction\_ring\_buffer.h](/server/include/miniredis/eviction_ring_buffer.h) | C++ | 49 | 1 | 12 | 62 |
| [server/include/miniredis/lfu\_tracker.h](/server/include/miniredis/lfu_tracker.h) | C++ | 172 | 11 | 40 | 223 |
| [server/include/miniredis/lru\_tracker.h](/server/include/miniredis/lru_tracker.h) | C++ | 111 | 6 | 22 | 139 |
| [server/include/miniredis/poller.h](/server/include/miniredis/poller.h) | C++ | 22 | 1 | 10 | 33 |
| [server/include/miniredis/resp.h](/server/include/miniredis/resp.h) | C++ | 87 | 2 | 18 | 107 |
| [server/include/miniredis/server.h](/server/include/miniredis/server.h) | C++ | 39 | 0 | 14 | 53 |
| [server/include/miniredis/store.h](/server/include/miniredis/store.h) | C++ | 112 | 7 | 33 | 152 |
| [server/include/miniredis/telemetry.h](/server/include/miniredis/telemetry.h) | C++ | 140 | 1 | 24 | 165 |
| [server/src/aof.cpp](/server/src/aof.cpp) | C++ | 217 | 9 | 44 | 270 |
| [server/src/command.cpp](/server/src/command.cpp) | C++ | 397 | 3 | 44 | 444 |
| [server/src/main.cpp](/server/src/main.cpp) | C++ | 28 | 0 | 7 | 35 |
| [server/src/poller.cpp](/server/src/poller.cpp) | C++ | 126 | 0 | 26 | 152 |
| [server/src/resp.cpp](/server/src/resp.cpp) | C++ | 300 | 6 | 46 | 352 |
| [server/src/server.cpp](/server/src/server.cpp) | C++ | 275 | 15 | 50 | 340 |
| [server/src/store.cpp](/server/src/store.cpp) | C++ | 483 | 3 | 93 | 579 |

[Summary](results.md) / Details / [Diff Summary](diff.md) / [Diff Details](diff-details.md)