#include "miniredis/store.h"

#include <algorithm>

namespace miniredis {

namespace {

// Glob pattern matcher supporting '*' (zero or more chars) and '?' (any single char)
bool match_pattern(std::string_view str, std::string_view pattern) {
    size_t s = 0, p = 0;
    size_t star_p = std::string_view::npos;
    size_t star_s = 0;

    while (s < str.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == str[s])) {
            ++s;
            ++p;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star_p = p++;
            star_s = s;
        } else if (star_p != std::string_view::npos) {
            p = star_p + 1;
            s = ++star_s;
        } else {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }

    return p == pattern.size();
}

} // namespace

Store::Store(ClockFn clock_fn)
    : clock_fn_(std::move(clock_fn)) {}

Store::TimePoint Store::current_time() const {
    if (clock_fn_) {
        return clock_fn_();
    }
    return std::chrono::steady_clock::now();
}

bool Store::is_expired(const Entry& entry) const {
    if (!entry.expires_at.has_value()) {
        return false;
    }
    return current_time() >= *entry.expires_at;
}

bool Store::check_and_expire(const std::string& key) {
    auto it = data_.find(key);
    if (it == data_.end()) {
        return false;
    }

    if (is_expired(it->second)) {
        expires_.erase(key);
        data_.erase(it);
        return true;
    }

    return false;
}

bool Store::set(const std::string& key,
                std::string value,
                std::optional<std::chrono::milliseconds> ttl,
                SetCondition cond) {
    // Passive expiration check first
    check_and_expire(key);

    const bool key_exists = (data_.find(key) != data_.end());
    if (cond == SetCondition::NX && key_exists) {
        return false;
    }
    if (cond == SetCondition::XX && !key_exists) {
        return false;
    }

    // Negative or zero TTL: immediate expiration/deletion per Redis spec
    if (ttl.has_value() && ttl->count() <= 0) {
        del(key);
        return true;
    }

    std::optional<TimePoint> expires_at = std::nullopt;
    if (ttl.has_value()) {
        expires_at = current_time() + *ttl;
        expires_.insert(key);
    } else {
        // Overwriting a key without TTL clears previous expiration
        expires_.erase(key);
    }

    data_[key] = Entry{std::move(value), expires_at};
    return true;
}

std::optional<std::string> Store::get(const std::string& key) {
    if (check_and_expire(key)) {
        return std::nullopt;
    }

    auto it = data_.find(key);
    if (it == data_.end()) {
        return std::nullopt;
    }

    return it->second.value;
}

bool Store::del(const std::string& key) {
    check_and_expire(key);

    auto it = data_.find(key);
    if (it == data_.end()) {
        return false;
    }

    expires_.erase(key);
    data_.erase(it);
    return true;
}

size_t Store::del(const std::vector<std::string>& keys) {
    size_t count = 0;
    for (const auto& key : keys) {
        if (del(key)) {
            ++count;
        }
    }
    return count;
}

bool Store::exists(const std::string& key) {
    if (check_and_expire(key)) {
        return false;
    }
    return data_.find(key) != data_.end();
}

size_t Store::exists(const std::vector<std::string>& keys) {
    size_t count = 0;
    for (const auto& key : keys) {
        if (exists(key)) {
            ++count;
        }
    }
    return count;
}

int64_t Store::ttl(const std::string& key) {
    int64_t ms = pttl(key);
    if (ms < 0) {
        return ms; // -1 or -2
    }
    // Round to seconds
    return (ms + 500) / 1000;
}

int64_t Store::pttl(const std::string& key) {
    if (check_and_expire(key)) {
        return -2;
    }

    auto it = data_.find(key);
    if (it == data_.end()) {
        return -2;
    }

    if (!it->second.expires_at.has_value()) {
        return -1;
    }

    auto diff = *it->second.expires_at - current_time();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();
    return (ms < 0) ? 0 : ms;
}

bool Store::expire(const std::string& key, std::chrono::seconds sec) {
    return pexpire(key, std::chrono::duration_cast<std::chrono::milliseconds>(sec));
}

bool Store::pexpire(const std::string& key, std::chrono::milliseconds ms) {
    if (check_and_expire(key)) {
        return false;
    }

    auto it = data_.find(key);
    if (it == data_.end()) {
        return false;
    }

    if (ms.count() <= 0) {
        del(key);
        return true;
    }

    it->second.expires_at = current_time() + ms;
    expires_.insert(key);
    return true;
}

bool Store::persist(const std::string& key) {
    if (check_and_expire(key)) {
        return false;
    }

    auto it = data_.find(key);
    if (it == data_.end() || !it->second.expires_at.has_value()) {
        return false;
    }

    it->second.expires_at = std::nullopt;
    expires_.erase(key);
    return true;
}

size_t Store::active_expire_cycle(size_t max_keys) {
    if (expires_.empty() || max_keys == 0) {
        return 0;
    }

    size_t total_purged = 0;
    constexpr size_t kMaxIterations = 16;
    size_t iterations = 0;

    // Redis-style adaptive loop: sample up to max_keys; if >25% expired, sample again up to limit
    while (iterations++ < kMaxIterations && !expires_.empty()) {
        std::vector<std::string> sample;
        sample.reserve(std::min(max_keys, expires_.size()));

        auto it = expires_.begin();
        while (it != expires_.end() && sample.size() < max_keys) {
            sample.push_back(*it);
            ++it;
        }

        size_t purged_in_iteration = 0;
        for (const auto& key : sample) {
            auto data_it = data_.find(key);
            if (data_it != data_.end() && is_expired(data_it->second)) {
                expires_.erase(key);
                data_.erase(data_it);
                ++purged_in_iteration;
            } else if (data_it == data_.end()) {
                // Key removed without clearing expires
                expires_.erase(key);
            }
        }

        total_purged += purged_in_iteration;

        // If less than 25% of sampled keys were expired, stop the cycle
        if (purged_in_iteration * 4 < sample.size()) {
            break;
        }
    }

    return total_purged;
}

size_t Store::dbsize() {
    // Passive check could be performed, but for performance dbsize reports data_.size()
    return data_.size();
}

void Store::flushdb() {
    data_.clear();
    expires_.clear();
}

std::vector<std::string> Store::keys(std::string_view pattern) {
    std::vector<std::string> matched;
    // Iterate and passively prune expired keys during keys enumeration
    auto it = data_.begin();
    while (it != data_.end()) {
        if (is_expired(it->second)) {
            expires_.erase(it->first);
            it = data_.erase(it);
        } else {
            if (pattern == "*" || match_pattern(it->first, pattern)) {
                matched.push_back(it->first);
            }
            ++it;
        }
    }
    return matched;
}

size_t Store::expires_size() const {
    return expires_.size();
}

} // namespace miniredis
