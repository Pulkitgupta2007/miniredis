#include "miniredis/store.h"

#include <algorithm>
#include <random>
#include <unordered_set>

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

void Store::add_or_update_expiry(const std::string& key, TimePoint exp) {
    expires_[key] = exp;
    if (expiry_index_.find(key) == expiry_index_.end()) {
        expiry_index_[key] = expiry_keys_.size();
        expiry_keys_.push_back(key);
    }
}

void Store::remove_expiry(const std::string& key) {
    expires_.erase(key);
    auto it = expiry_index_.find(key);
    if (it != expiry_index_.end()) {
        size_t idx = it->second;
        size_t last_idx = expiry_keys_.size() - 1;
        if (idx != last_idx) {
            const std::string& last_key = expiry_keys_[last_idx];
            expiry_keys_[idx] = last_key;
            expiry_index_[last_key] = idx;
        }
        expiry_keys_.pop_back();
        expiry_index_.erase(it);
    }
}

bool Store::check_and_expire(const std::string& key) {
    auto it = data_.find(key);
    if (it == data_.end()) {
        return false;
    }

    if (is_expired(it->second)) {
        remove_expiry(key);
        data_.erase(it);
        return true;
    }

    return false;
}

bool Store::set(const std::string& key,
                std::string value,
                std::optional<std::chrono::milliseconds> ttl,
                SetCondition cond) {
    // Lazy expiration check first
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
        add_or_update_expiry(key, *expires_at);
    } else {
        // Overwriting a key without TTL clears prior expiration
        remove_expiry(key);
    }

    data_[key] = Entry{std::move(value), expires_at};
    return true;
}

std::optional<std::string> Store::get(const std::string& key) {
    // Lazy expiration on access
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

    remove_expiry(key);
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
    // Lazy expiration on access
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

    TimePoint exp = current_time() + ms;
    it->second.expires_at = exp;
    add_or_update_expiry(key, exp);
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
    remove_expiry(key);
    return true;
}

ActiveExpireStats Store::active_expire_cycle(size_t sample_size, std::chrono::milliseconds time_budget) {
    ActiveExpireStats stats;
    if (expiry_keys_.empty() || sample_size == 0) {
        return stats;
    }

    auto start_time = current_time();
    constexpr size_t kMaxIterations = 16;
    std::mt19937 rng(42);

    while (stats.iterations < kMaxIterations && !expiry_keys_.empty()) {
        stats.iterations++;

        size_t current_sample_size = std::min(sample_size, expiry_keys_.size());
        size_t expired_in_iteration = 0;

        // Collect sample_size random keys without replacement
        std::vector<std::string> sample;
        sample.reserve(current_sample_size);

        if (current_sample_size == expiry_keys_.size()) {
            sample = expiry_keys_;
        } else {
            std::unordered_set<size_t> chosen_indices;
            std::uniform_int_distribution<size_t> dist(0, expiry_keys_.size() - 1);
            while (chosen_indices.size() < current_sample_size) {
                chosen_indices.insert(dist(rng));
            }
            for (size_t idx : chosen_indices) {
                sample.push_back(expiry_keys_[idx]);
            }
        }

        // Purge expired keys found in the random sample
        for (const auto& key : sample) {
            stats.sampled_keys++;
            auto it = data_.find(key);
            if (it != data_.end() && is_expired(it->second)) {
                remove_expiry(key);
                data_.erase(it);
                ++stats.purged_keys;
                ++expired_in_iteration;
            } else if (it == data_.end()) {
                remove_expiry(key);
            }
        }

        // Check elapsed time against time budget
        if (current_time() - start_time >= time_budget) {
            stats.timelimit_reached = true;
            break;
        }

        // If <= 25% of sampled keys were expired, stop cycle (Redis adaptive threshold)
        if (expired_in_iteration * 4 <= current_sample_size) {
            break;
        }
    }

    return stats;
}

size_t Store::dbsize() {
    return data_.size();
}

void Store::flushdb() {
    data_.clear();
    expires_.clear();
    expiry_keys_.clear();
    expiry_index_.clear();
}

void Store::flushall() {
    flushdb();
}

std::string Store::ping(std::optional<std::string_view> message) {
    if (message.has_value()) {
        return std::string(*message);
    }
    return "PONG";
}

std::vector<std::string> Store::keys(std::string_view pattern) {
    std::vector<std::string> matched;
    auto it = data_.begin();
    while (it != data_.end()) {
        if (is_expired(it->second)) {
            remove_expiry(it->first);
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
    return expiry_keys_.size();
}

} // namespace miniredis
