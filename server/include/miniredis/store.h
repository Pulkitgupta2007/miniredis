#pragma once

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

    // Inject custom clock function for deterministic tests; defaults to steady_clock::now
    explicit Store(ClockFn clock_fn = nullptr);

    // Core Key-Value Operations
    bool set(const std::string& key,
             std::string value,
             std::optional<std::chrono::milliseconds> ttl = std::nullopt,
             SetCondition cond = SetCondition::NONE);

    std::optional<std::string> get(const std::string& key);

    bool del(const std::string& key);
    size_t del(const std::vector<std::string>& keys);

    bool exists(const std::string& key);
    size_t exists(const std::vector<std::string>& keys);

    // TTL Operations
    // Returns:
    //  -2 if key does not exist or has expired
    //  -1 if key exists but has no expiration
    //  >= 0 remaining time (seconds for ttl, milliseconds for pttl)
    int64_t ttl(const std::string& key);
    int64_t pttl(const std::string& key);

    bool expire(const std::string& key, std::chrono::seconds sec);
    bool pexpire(const std::string& key, std::chrono::milliseconds ms);
    bool persist(const std::string& key);

    // Active Expiration Cycle
    // Samples random keys with TTL (not a full scan), repeating if > 25% of sample is expired,
    // bounded by a per-cycle time budget and maximum iteration ceiling.
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

    TimePoint current_time() const;
    bool is_expired(const Entry& entry) const;
    bool check_and_expire(const std::string& key);

    // Swap-and-pop expiry tracking for O(1) insertion, deletion, and random sampling
    void add_or_update_expiry(const std::string& key, TimePoint exp);
    void remove_expiry(const std::string& key);

    ClockFn clock_fn_;
    std::unordered_map<std::string, Entry> data_;

    // Expiry tracking structures:
    // 1. Map of key -> expiration TimePoint
    // 2. Contiguous array of expiring keys for uniform O(1) random sampling
    // 3. Reverse index map of key -> position in expiry_keys_ for O(1) swap-and-pop removal
    std::unordered_map<std::string, TimePoint> expires_;
    std::vector<std::string> expiry_keys_;
    std::unordered_map<std::string, size_t> expiry_index_;
};

} // namespace miniredis
