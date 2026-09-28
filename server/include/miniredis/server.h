#pragma once

#include "miniredis/poller.h"
#include "miniredis/store.h"

#include <atomic>
#include <string>
#include <unordered_map>

namespace miniredis {

struct ClientSession {
    int fd{-1};
    std::string in_buf{};
    std::string out_buf{};
    bool should_close{false};
};

class Server {
public:
    Server(std::string host, int port, Store& store);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start();
    void stop();

private:
    void init_signal_pipe();
    void close_client(int fd);
    void handle_accept();
    void handle_client_read(int fd);
    void handle_client_write(int fd);

    std::string host_;
    int port_;
    Store& store_;

    int listen_fd_{-1};
    int signal_pipe_[2]{-1, -1};
    std::atomic<bool> running_{false};

    Poller poller_;
    std::unordered_map<int, ClientSession> clients_;
};

} // namespace miniredis
