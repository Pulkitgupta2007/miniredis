#pragma once

#include <vector>

namespace miniredis {

struct PollEvent {
    int fd{-1};
    bool readable{false};
    bool writable{false};
};

// Unified reactor poller: uses Linux epoll on Linux, BSD/Darwin kqueue on macOS
class Poller {
public:
    Poller();
    ~Poller();

    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;

    bool add(int fd, bool read, bool write);
    bool modify(int fd, bool read, bool write);
    bool remove(int fd);

    std::vector<PollEvent> poll(int timeout_ms = -1);

private:
    int poll_fd_{-1};
};

} // namespace miniredis
