#include "miniredis/poller.h"

#include <unistd.h>

#if defined(__linux__)
#include <sys/epoll.h>

namespace miniredis {

Poller::Poller() {
    poll_fd_ = epoll_create1(EPOLL_CLOEXEC);
}

Poller::~Poller() {
    if (poll_fd_ >= 0) {
        close(poll_fd_);
    }
}

bool Poller::add(int fd, bool read, bool write) {
    struct epoll_event ev{};
    ev.data.fd = fd;
    if (read) ev.events |= EPOLLIN;
    if (write) ev.events |= EPOLLOUT;
    return epoll_ctl(poll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
}

bool Poller::modify(int fd, bool read, bool write) {
    struct epoll_event ev{};
    ev.data.fd = fd;
    if (read) ev.events |= EPOLLIN;
    if (write) ev.events |= EPOLLOUT;
    return epoll_ctl(poll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

bool Poller::remove(int fd) {
    return epoll_ctl(poll_fd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

std::vector<PollEvent> Poller::poll(int timeout_ms) {
    constexpr int kMaxEvents = 64;
    struct epoll_event events[kMaxEvents];
    int n = epoll_wait(poll_fd_, events, kMaxEvents, timeout_ms);

    std::vector<PollEvent> ready;
    if (n <= 0) {
        return ready;
    }

    ready.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        PollEvent pe;
        pe.fd = events[i].data.fd;
        pe.readable = (events[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR)) != 0;
        pe.writable = (events[i].events & EPOLLOUT) != 0;
        ready.push_back(pe);
    }
    return ready;
}

} // namespace miniredis

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <sys/event.h>
#include <sys/time.h>

namespace miniredis {

Poller::Poller() {
    poll_fd_ = kqueue();
}

Poller::~Poller() {
    if (poll_fd_ >= 0) {
        close(poll_fd_);
    }
}

bool Poller::add(int fd, bool read, bool write) {
    struct kevent changes[2];
    int nchanges = 0;
    if (read) {
        EV_SET(&changes[nchanges++], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }
    if (write) {
        EV_SET(&changes[nchanges++], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }
    if (nchanges == 0) {
        return true;
    }
    return kevent(poll_fd_, changes, nchanges, nullptr, 0, nullptr) == 0;
}

bool Poller::modify(int fd, bool read, bool write) {
    struct kevent changes[2];
    int nchanges = 0;
    EV_SET(&changes[nchanges++], fd, EVFILT_READ, read ? (EV_ADD | EV_ENABLE) : (EV_DELETE | EV_DISABLE), 0, 0, nullptr);
    EV_SET(&changes[nchanges++], fd, EVFILT_WRITE, write ? (EV_ADD | EV_ENABLE) : (EV_DELETE | EV_DISABLE), 0, 0, nullptr);
    kevent(poll_fd_, changes, nchanges, nullptr, 0, nullptr);
    return true;
}

bool Poller::remove(int fd) {
    struct kevent changes[2];
    EV_SET(&changes[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&changes[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    kevent(poll_fd_, changes, 2, nullptr, 0, nullptr);
    return true;
}

std::vector<PollEvent> Poller::poll(int timeout_ms) {
    constexpr int kMaxEvents = 64;
    struct kevent events[kMaxEvents];
    struct timespec ts{};
    struct timespec* ts_ptr = nullptr;

    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1'000'000L;
        ts_ptr = &ts;
    }

    int n = kevent(poll_fd_, nullptr, 0, events, kMaxEvents, ts_ptr);
    std::vector<PollEvent> ready;
    if (n <= 0) {
        return ready;
    }

    ready.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        PollEvent pe;
        pe.fd = static_cast<int>(events[i].ident);
        if (events[i].filter == EVFILT_READ) {
            pe.readable = true;
        }
        if (events[i].filter == EVFILT_WRITE) {
            pe.writable = true;
        }
        if (events[i].flags & (EV_EOF | EV_ERROR)) {
            pe.readable = true;
        }
        ready.push_back(pe);
    }
    return ready;
}

} // namespace miniredis

#else
#error "Unsupported operating system multiplexer. Requires Linux (epoll) or macOS/BSD (kqueue)."
#endif
