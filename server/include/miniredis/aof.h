#pragma once

#include "miniredis/store.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace miniredis {

enum class FsyncPolicy {
    ALWAYS,
    EVERYSEC,
    NO
};

class AofManager {
public:
    using EpochClockFn = std::function<int64_t()>; // Returns current Unix epoch milliseconds

    explicit AofManager(std::string filename,
                        FsyncPolicy policy = FsyncPolicy::EVERYSEC,
                        EpochClockFn clock_fn = nullptr);
    ~AofManager();

    AofManager(const AofManager&) = delete;
    AofManager& operator=(const AofManager&) = delete;

    bool open();
    void close();

    // Appends a write command to the AOF file in RESP format
    bool append_command(const std::vector<std::string>& args);

    struct ReplayResult {
        size_t commands_replayed{0};
        bool truncated_recovered{false};
        bool success{false};
        std::string error_message{};
    };

    // Replays the AOF log from disk into the store
    ReplayResult replay(Store& store);

    // Rewrites / compacts the current live state of the store into a minimal AOF log
    // Swapped atomically via rename()
    bool rewrite(Store& store);

    void set_fsync_policy(FsyncPolicy policy);
    FsyncPolicy get_fsync_policy() const noexcept;
    std::string get_fsync_policy_str() const;

    const std::string& filename() const noexcept { return filename_; }
    size_t file_size() const;
    void sync(); // Explicit fsync call

private:
    int64_t current_epoch_ms() const;
    void fsync_thread_worker();

    std::string filename_;
    FsyncPolicy policy_;
    EpochClockFn clock_fn_;
    int fd_{-1};

    std::atomic<bool> running_{false};
    std::thread worker_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace miniredis
