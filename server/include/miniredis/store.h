#pragma once

#include "miniredis/eviction_ring_buffer.h"
#include "miniredis/lfu_tracker.h"
#include "miniredis/lru_tracker.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace miniredis {

enum class SetCondition {
    NONE, // Normal SET: overwrite unconditionally
    NX,   // Set only if key does NOT exist
    XX    // Set only if key ALREADY exists
};

enum class EvictionPolicy {
    NOEVICTION,
    ALLKEYS_LRU,
    ALLKEYS_LFU
};

struct SetResult {
    bool ok{false};
    bool oom{false};

    operator bool() const noexcept {
        return ok;
    }
};

struct ActiveExpireStats {
    size_t sampled_keys{0};
    size_t purged_keys{0};
    size_t iterations{0};
    bool timelimit_reached{false};
};

class Store {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    using ClockFn = std::function<TimePoint()>;

    explicit Store(ClockFn clock_fn = nullptr);

    // Core Key-Value Operations
    SetResult set(const std::string& key,
                  std::string value,
                  std::optional<std::chrono::milliseconds> ttl = std::nullopt,
                  SetCondition cond = SetCondition::NONE);

    std::optional<std::string> get(const std::string& key);

    bool del(const std::string& key);
    size_t del(const std::vector<std::string>& keys);

    bool exists(const std::string& key);
    size_t exists(const std::vector<std::string>& keys);

    // Memory & Eviction Management
    bool set_maxmemory(size_t bytes);
    size_t get_maxmemory() const noexcept;

    bool set_maxmemory_policy(std::string_view policy_str);
    std::string get_maxmemory_policy() const;
    EvictionPolicy get_eviction_policy_enum() const noexcept;

    size_t used_memory() const noexcept;
    std::optional<size_t> memory_usage(const std::string& key);
    const EvictionRingBuffer& eviction_buffer() const noexcept;

    // TTL Operations
    int64_t ttl(const std::string& key);
    int64_t pttl(const std::string& key);

    bool expire(const std::string& key, std::chrono::seconds sec);
    bool pexpire(const std::string& key, std::chrono::milliseconds ms);
    bool expireat(const std::string& key, int64_t epoch_sec, int64_t now_epoch_sec = 0);
    bool pexpireat(const std::string& key, int64_t epoch_ms, int64_t now_epoch_ms = 0);
    bool persist(const std::string& key);

    ActiveExpireStats active_expire_cycle(
        size_t sample_size = 20,
        std::chrono::milliseconds time_budget = std::chrono::milliseconds(25));

    // Database Inspection & Management
    size_t dbsize();
    void flushdb();
    void flushall();
    std::string ping(std::optional<std::string_view> message = std::nullopt);
    std::vector<std::string> keys(std::string_view pattern = "*");
    size_t expires_size() const;

private:
    struct Entry {
        std::string value;
        std::optional<TimePoint> expires_at;
    };

    static constexpr size_t kEntryOverhead = 64; // Per-entry overhead for hash node and pointers
    static size_t estimate_memory(std::string_view key, std::string_view value) noexcept {
        return key.size() + value.size() + kEntryOverhead;
    }

    TimePoint current_time() const;
    int64_t current_time_ms() const;
    bool is_expired(const Entry& entry) const;
    bool check_and_expire(const std::string& key);

    void add_or_update_expiry(const std::string& key, TimePoint exp);
    void remove_expiry(const std::string& key);
    bool evict_keys_if_needed(size_t needed_bytes);

    ClockFn clock_fn_;
    std::unordered_map<std::string, Entry> data_;

    // Expiry tracking
    std::unordered_map<std::string, TimePoint> expires_;
    std::vector<std::string> expiry_keys_;
    std::unordered_map<std::string, size_t> expiry_index_;

    // Memory & Eviction structures
    size_t maxmemory_{0}; // 0 = no limit
    size_t used_memory_{0};
    EvictionPolicy eviction_policy_{EvictionPolicy::NOEVICTION};

    LruTracker lru_tracker_;
    LfuTracker lfu_tracker_;
    EvictionRingBuffer eviction_buffer_{256};
};

} // namespace miniredis
