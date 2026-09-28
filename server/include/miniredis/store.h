#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace miniredis {

enum class SetCondition {
    NONE, // Normal SET: overwrite unconditionally
    NX,   // Set only if key does NOT exist
    XX    // Set only if key ALREADY exists
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

    // Expiration Cycles
    // Probabilistically samples up to max_keys from keys with TTLs and purges expired entries.
    // Returns count of purged keys.
    size_t active_expire_cycle(size_t max_keys = 20);

    // Database Inspection & Management
    size_t dbsize();
    void flushdb();
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

    ClockFn clock_fn_;
    std::unordered_map<std::string, Entry> data_;
    std::unordered_set<std::string> expires_; // Tracks keys that have an active TTL
};

} // namespace miniredis
