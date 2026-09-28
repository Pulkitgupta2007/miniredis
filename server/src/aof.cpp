#include "miniredis/aof.h"
#include "miniredis/command.h"
#include "miniredis/resp.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

namespace miniredis {

AofManager::AofManager(std::string filename, FsyncPolicy policy, EpochClockFn clock_fn)
    : filename_(std::move(filename)), policy_(policy), clock_fn_(std::move(clock_fn)) {}

AofManager::~AofManager() {
    close();
}

int64_t AofManager::current_epoch_ms() const {
    if (clock_fn_) {
        return clock_fn_();
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool AofManager::open() {
    if (fd_ >= 0) {
        return true;
    }

    fd_ = ::open(filename_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
        std::cerr << "[AOF] Failed to open AOF file " << filename_ << ": " << std::strerror(errno) << "\n";
        return false;
    }

    running_ = true;
    if (policy_ == FsyncPolicy::EVERYSEC) {
        worker_thread_ = std::thread(&AofManager::fsync_thread_worker, this);
    }
    return true;
}

void AofManager::close() {
    if (running_) {
        running_ = false;
        cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    if (fd_ >= 0) {
        sync();
        ::close(fd_);
        fd_ = -1;
    }
}

void AofManager::sync() {
    if (fd_ >= 0) {
        ::fsync(fd_);
    }
}

void AofManager::fsync_thread_worker() {
    while (running_) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::seconds(1), [this]() { return !running_; });
        if (fd_ >= 0 && policy_ == FsyncPolicy::EVERYSEC) {
            ::fsync(fd_);
        }
    }
}

bool AofManager::append_command(const std::vector<std::string>& args) {
    if (fd_ < 0) {
        if (!open()) {
            return false;
        }
    }

    std::string frame = RespSerializer::serialize_array(args);
    ssize_t written = ::write(fd_, frame.data(), frame.size());
    if (written < 0 || static_cast<size_t>(written) != frame.size()) {
        std::cerr << "[AOF] Write error: " << std::strerror(errno) << "\n";
        return false;
    }

    if (policy_ == FsyncPolicy::ALWAYS) {
        ::fsync(fd_);
    }
    return true;
}

size_t AofManager::file_size() const {
    struct stat st{};
    if (::stat(filename_.c_str(), &st) == 0) {
        return static_cast<size_t>(st.st_size);
    }
    return 0;
}

AofManager::ReplayResult AofManager::replay(Store& store) {
    ReplayResult res;
    int read_fd = ::open(filename_.c_str(), O_RDONLY);
    if (read_fd < 0) {
        // File doesn't exist yet: clean empty state
        res.success = true;
        return res;
    }

    struct stat st{};
    if (::fstat(read_fd, &st) != 0) {
        res.success = false;
        res.error_message = "Failed to stat AOF file";
        ::close(read_fd);
        return res;
    }

    size_t total_size = static_cast<size_t>(st.st_size);
    if (total_size == 0) {
        res.success = true;
        ::close(read_fd);
        return res;
    }

    std::string buffer;
    buffer.resize(total_size);
    ssize_t bytes_read = ::read(read_fd, buffer.data(), total_size);
    ::close(read_fd);

    if (bytes_read < 0 || static_cast<size_t>(bytes_read) != total_size) {
        res.success = false;
        res.error_message = "Failed to read entire AOF file";
        return res;
    }

    size_t offset = 0;
    size_t last_valid_offset = 0;

    while (offset < buffer.size()) {
        std::string_view slice(buffer.data() + offset, buffer.size() - offset);
        auto parse_res = RespParser::parse_command(slice);

        if (parse_res.status == ParseStatus::Ok) {
            std::vector<std::string> args;
            if (parse_res.value.type == RespType::Array) {
                for (const auto& elem : parse_res.value.elements) {
                    args.push_back(elem.str_val);
                }
            }

            CommandDispatcher::execute(args, store);
            offset += parse_res.bytes_consumed;
            last_valid_offset = offset;
            ++res.commands_replayed;
        } else {
            // Reached incomplete or corrupted record at the end of the file
            std::cerr << "[AOF WARNING] Detected truncated record at offset " << offset
                      << " of " << total_size << ". Truncating to last valid offset: "
                      << last_valid_offset << ".\n";

            // Truncate file to last valid record
            if (::truncate(filename_.c_str(), static_cast<off_t>(last_valid_offset)) == 0) {
                res.truncated_recovered = true;
            } else {
                std::cerr << "[AOF ERROR] Failed to truncate corrupted AOF: " << std::strerror(errno) << "\n";
            }
            break;
        }
    }

    res.success = true;
    return res;
}

bool AofManager::rewrite(Store& store) {
    std::string tmp_filename = filename_ + ".rewrite.tmp";
    int tmp_fd = ::open(tmp_filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (tmp_fd < 0) {
        std::cerr << "[AOF] Failed to create temp rewrite file: " << std::strerror(errno) << "\n";
        return false;
    }

    // Iterate all active keys in the database
    auto all_keys = store.keys("*");
    int64_t now_ms = current_epoch_ms();

    for (const auto& key : all_keys) {
        auto val = store.get(key);
        if (!val.has_value()) {
            continue; // Skip expired or missing keys
        }

        // 1. Emit SET command
        std::vector<std::string> set_args = {"SET", key, *val};
        std::string set_frame = RespSerializer::serialize_array(set_args);
        (void)::write(tmp_fd, set_frame.data(), set_frame.size());

        // 2. Emit absolute expiration if key has a TTL
        int64_t remaining_ms = store.pttl(key);
        if (remaining_ms > 0) {
            int64_t absolute_expire_ms = now_ms + remaining_ms;
            std::vector<std::string> expire_args = {"PEXPIREAT", key, std::to_string(absolute_expire_ms)};
            std::string exp_frame = RespSerializer::serialize_array(expire_args);
            (void)::write(tmp_fd, exp_frame.data(), exp_frame.size());
        }
    }

    // Ensure all compacted bytes are synchronized to storage media
    ::fsync(tmp_fd);
    ::close(tmp_fd);

    // Atomic replace via POSIX rename
    if (::rename(tmp_filename.c_str(), filename_.c_str()) != 0) {
        std::cerr << "[AOF] Failed to atomically rename rewrite file: " << std::strerror(errno) << "\n";
        return false;
    }

    // Reopen main file descriptor if currently open
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = ::open(filename_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    }

    return true;
}

void AofManager::set_fsync_policy(FsyncPolicy policy) {
    if (policy_ == policy) {
        return;
    }

    if (policy_ == FsyncPolicy::EVERYSEC && policy != FsyncPolicy::EVERYSEC) {
        running_ = false;
        cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    policy_ = policy;

    if (policy_ == FsyncPolicy::EVERYSEC && fd_ >= 0) {
        running_ = true;
        worker_thread_ = std::thread(&AofManager::fsync_thread_worker, this);
    }
}

FsyncPolicy AofManager::get_fsync_policy() const noexcept {
    return policy_;
}

std::string AofManager::get_fsync_policy_str() const {
    switch (policy_) {
        case FsyncPolicy::ALWAYS:
            return "always";
        case FsyncPolicy::EVERYSEC:
            return "everysec";
        case FsyncPolicy::NO:
            return "no";
    }
    return "everysec";
}

} // namespace miniredis
