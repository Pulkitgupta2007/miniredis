#include "miniredis/server.h"
#include "miniredis/command.h"
#include "miniredis/resp.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <iostream>

#include "miniredis/aof.h"

namespace miniredis {

namespace {

static int g_signal_write_fd = -1;

void sigint_handler(int sig) {
    (void)sig;
    if (g_signal_write_fd >= 0) {
        char byte = 'q';
        // write() is guaranteed async-signal-safe by POSIX
        (void)::write(g_signal_write_fd, &byte, 1);
    }
}

bool set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

} // namespace

Server::Server(std::string host, int port, Store& store, AofManager* aof)
    : host_(std::move(host)), port_(port), store_(store), aof_(aof) {}

Server::~Server() {
    stop();
}

void Server::init_signal_pipe() {
    if (::pipe(signal_pipe_) != 0) {
        std::cerr << "Failed to create signal pipe\n";
        return;
    }
    set_nonblocking(signal_pipe_[0]);
    set_nonblocking(signal_pipe_[1]);

    g_signal_write_fd = signal_pipe_[1];

    struct sigaction sa{};
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    // Ignore SIGPIPE so writing to a closed socket does not kill the server
    signal(SIGPIPE, SIG_IGN);

    poller_.add(signal_pipe_[0], true, false);
}

bool Server::start() {
    // Replay AOF on startup if configured
    if (aof_) {
        std::cout << "[MiniRedis] Replaying AOF log from " << aof_->filename() << "...\n";
        auto replay_res = aof_->replay(store_);
        std::cout << "[MiniRedis] Replayed " << replay_res.commands_replayed << " commands from AOF.\n";
        if (replay_res.truncated_recovered) {
            std::cout << "[MiniRedis] Recovered from truncated AOF log tail successfully.\n";
        }
        aof_->open();
    }

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::cerr << "Failed to create socket: " << std::strerror(errno) << "\n";
        return false;
    }

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_NOSIGPIPE
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif

    if (!set_nonblocking(listen_fd_)) {
        std::cerr << "Failed to set listen socket non-blocking\n";
        ::close(listen_fd_);
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "Invalid address: " << host_ << "\n";
        ::close(listen_fd_);
        return false;
    }

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "Failed to bind to " << host_ << ":" << port_ << ": " << std::strerror(errno) << "\n";
        ::close(listen_fd_);
        return false;
    }

    if (::listen(listen_fd_, 512) != 0) {
        std::cerr << "Failed to listen: " << std::strerror(errno) << "\n";
        ::close(listen_fd_);
        return false;
    }

    poller_.add(listen_fd_, true, false);
    init_signal_pipe();

    running_ = true;
    std::cout << "[MiniRedis] Server listening on " << host_ << ":" << port_ << " (Ready for connections)\n";

    while (running_) {
        // Poll with 100ms timeout
        auto events = poller_.poll(100);

        // Run active expiration cycle periodically during event loop idle ticks
        store_.active_expire_cycle(20);

        for (const auto& ev : events) {
            if (ev.fd == signal_pipe_[0]) {
                char dummy;
                (void)::read(signal_pipe_[0], &dummy, 1);
                std::cout << "\n[MiniRedis] Received signal, shutting down gracefully...\n";
                running_ = false;
                break;
            } else if (ev.fd == listen_fd_) {
                handle_accept();
            } else {
                if (ev.readable) {
                    handle_client_read(ev.fd);
                }
                if (ev.writable) {
                    handle_client_write(ev.fd);
                }
            }
        }
    }

    stop();
    return true;
}

void Server::stop() {
    running_ = false;

    if (aof_) {
        aof_->close();
    }

    // Close all connected clients
    for (auto& [fd, session] : clients_) {
        poller_.remove(fd);
        ::close(fd);
    }
    clients_.clear();

    if (listen_fd_ >= 0) {
        poller_.remove(listen_fd_);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (signal_pipe_[0] >= 0) {
        poller_.remove(signal_pipe_[0]);
        ::close(signal_pipe_[0]);
        ::close(signal_pipe_[1]);
        signal_pipe_[0] = -1;
        signal_pipe_[1] = -1;
        g_signal_write_fd = -1;
    }
}

void Server::handle_accept() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // No more pending connections
            }
            std::cerr << "Accept error: " << std::strerror(errno) << "\n";
            break;
        }

        set_nonblocking(client_fd);
#ifdef SO_NOSIGPIPE
        int opt = 1;
        ::setsockopt(client_fd, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif
        poller_.add(client_fd, true, false);
        clients_[client_fd] = ClientSession{client_fd, "", "", false};
    }
}

void Server::close_client(int fd) {
    poller_.remove(fd);
    ::close(fd);
    clients_.erase(fd);
}

void Server::handle_client_read(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return;
    }
    auto& client = it->second;

    char buf[16384];
    while (true) {
        ssize_t bytes_read = ::read(fd, buf, sizeof(buf));
        if (bytes_read > 0) {
            client.in_buf.append(buf, static_cast<size_t>(bytes_read));
        } else if (bytes_read == 0) {
            // Client closed connection (EOF)
            close_client(fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // All currently available data read
            }
            // Unexpected read error
            close_client(fd);
            return;
        }
    }

    // Process all fully buffered commands (handles fragmented & pipelined input)
    while (!client.in_buf.empty()) {
        auto parse_res = RespParser::parse_command(client.in_buf);
        if (parse_res.status == ParseStatus::Incomplete) {
            if (client.in_buf.size() > 64 * 1024 * 1024) {
                client.out_buf += RespSerializer::serialize_error("ERR Protocol error: request buffer exceeded maximum limit");
                client.should_close = true;
                handle_client_write(fd);
                close_client(fd);
                return;
            }
            break; // Await more fragments from socket
        }

        if (parse_res.status == ParseStatus::Error) {
            client.out_buf += RespSerializer::serialize_error("ERR Protocol error: " + parse_res.error_message);
            client.in_buf.clear();
            break;
        }

        if (parse_res.bytes_consumed == 0) {
            client.in_buf.clear();
            break;
        }

        // Consume parsed frame
        client.in_buf.erase(0, parse_res.bytes_consumed);

        // Extract command arguments
        std::vector<std::string> args;
        if (parse_res.value.type == RespType::Array) {
            for (const auto& elem : parse_res.value.elements) {
                if (elem.type == RespType::BulkString || elem.type == RespType::SimpleString) {
                    args.push_back(elem.str_val);
                }
            }
        }

        auto cmd_res = CommandDispatcher::execute(args, store_, clients_.size());
        client.out_buf += cmd_res.response;
        if (cmd_res.should_close) {
            client.should_close = true;
        }

        // Log mutating commands to AOF if enabled and command succeeded
        if (aof_ && !args.empty() && !cmd_res.response.empty() && cmd_res.response[0] != '-') {
            std::string cmd = args[0];
            for (char& c : cmd) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            if (cmd == "SET" || cmd == "DEL" || cmd == "EXPIRE" || cmd == "PEXPIRE" ||
                cmd == "EXPIREAT" || cmd == "PEXPIREAT" || cmd == "PERSIST" ||
                cmd == "FLUSHALL" || cmd == "FLUSHDB") {
                aof_->append_command(args);
            } else if (cmd == "BGREWRITEAOF") {
                aof_->rewrite(store_);
            }
        }
    }

    if (!client.out_buf.empty()) {
        handle_client_write(fd);
    }
}

void Server::handle_client_write(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return;
    }
    auto& client = it->second;

    while (!client.out_buf.empty()) {
        ssize_t written = ::write(fd, client.out_buf.data(), client.out_buf.size());
        if (written > 0) {
            client.out_buf.erase(0, static_cast<size_t>(written));
        } else if (written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Buffer full; arm writable event in poller
                poller_.modify(fd, true, true);
                return;
            }
            close_client(fd);
            return;
        }
    }

    // Output buffer drained
    if (client.should_close) {
        close_client(fd);
    } else {
        // Disarm writable event, keep reading
        poller_.modify(fd, true, false);
    }
}

} // namespace miniredis
