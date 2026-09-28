#include "miniredis/store.h"

#include <algorithm>
#include <random>
#include <unordered_set>

namespace miniredis {

namespace {

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

int64_t Store::current_time_ms() const {
    auto diff = current_time().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();
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
        size_t mem = estimate_memory(key, it->second.value);
        if (used_memory_ >= mem) {
            used_memory_ -= mem;
        } else {
            used_memory_ = 0;
        }

        metrics_.record_expired(1);
        telemetry_buffer_.push(TelemetryEvent{current_time_ms(), TelemetryEventType::EXPIRATION, key, "ttl_expired"});

        remove_expiry(key);
        lru_tracker_.remove(key);
        lfu_tracker_.remove(key);
        data_.erase(it);
        return true;
    }

    return false;
}

bool Store::evict_keys_if_needed(size_t needed_bytes) {
    if (maxmemory_ == 0) {
        return true;
    }

    while (used_memory_ + needed_bytes > maxmemory_ && !data_.empty()) {
        if (eviction_policy_ == EvictionPolicy::NOEVICTION) {
            return false; // Cannot evict under noeviction
        }

        std::optional<std::string> victim;
        if (eviction_policy_ == EvictionPolicy::ALLKEYS_LRU) {
            victim = lru_tracker_.pop_lru();
        } else if (eviction_policy_ == EvictionPolicy::ALLKEYS_LFU) {
            victim = lfu_tracker_.pop_lfu();
        }

        if (!victim.has_value()) {
            return false;
        }

        auto it = data_.find(*victim);
        if (it != data_.end()) {
            size_t freed = estimate_memory(*victim, it->second.value);
            if (used_memory_ >= freed) {
                used_memory_ -= freed;
            } else {
                used_memory_ = 0;
            }

            remove_expiry(*victim);
            if (eviction_policy_ == EvictionPolicy::ALLKEYS_LRU) {
                lfu_tracker_.remove(*victim);
            } else {
                lru_tracker_.remove(*victim);
            }

            data_.erase(it);
            auto ev = EvictionEvent{*victim, get_maxmemory_policy(), current_time_ms(), freed};
            eviction_buffer_.push(ev);
            telemetry_buffer_.push(TelemetryEvent{current_time_ms(), TelemetryEventType::EVICTION, *victim, "freed=" + std::to_string(freed)});
        }
    }

    return (used_memory_ + needed_bytes <= maxmemory_);
}

SetResult Store::set(const std::string& key,
                     std::string value,
                     std::optional<std::chrono::milliseconds> ttl,
                     SetCondition cond) {
    check_and_expire(key);

    const bool key_exists = (data_.find(key) != data_.end());
    if (cond == SetCondition::NX && key_exists) {
        return SetResult{false, false};
    }
    if (cond == SetCondition::XX && !key_exists) {
        return SetResult{false, false};
    }

    // Negative/zero TTL: immediate deletion per Redis spec
    if (ttl.has_value() && ttl->count() <= 0) {
        del(key);
        return SetResult{true, false};
    }

    size_t new_mem = estimate_memory(key, value);
    size_t old_mem = 0;
    auto existing_it = data_.find(key);
    if (existing_it != data_.end()) {
        old_mem = estimate_memory(key, existing_it->second.value);
    }

    size_t delta = (new_mem > old_mem) ? (new_mem - old_mem) : 0;
    if (!evict_keys_if_needed(delta)) {
        return SetResult{false, true}; // OOM error!
    }

    // Update memory accounting
    if (new_mem >= old_mem) {
        used_memory_ += (new_mem - old_mem);
    } else {
        used_memory_ -= (old_mem - new_mem);
    }

    // Update LRU and LFU trackers
    lru_tracker_.add(key);
    lfu_tracker_.add(key);

    std::optional<TimePoint> expires_at = std::nullopt;
    if (ttl.has_value()) {
        expires_at = current_time() + *ttl;
        add_or_update_expiry(key, *expires_at);
    } else {
        remove_expiry(key);
    }

    data_[key] = Entry{std::move(value), expires_at};
    return SetResult{true, false};
}

std::optional<std::string> Store::get(const std::string& key) {
    if (check_and_expire(key)) {
        metrics_.record_miss();
        return std::nullopt;
    }

    auto it = data_.find(key);
    if (it == data_.end()) {
        metrics_.record_miss();
        return std::nullopt;
    }

    metrics_.record_hit();
    lru_tracker_.touch(key);
    lfu_tracker_.touch(key);

    return it->second.value;
}

bool Store::del(const std::string& key) {
    check_and_expire(key);

    auto it = data_.find(key);
    if (it == data_.end()) {
        return false;
    }

    size_t mem = estimate_memory(key, it->second.value);
    if (used_memory_ >= mem) {
        used_memory_ -= mem;
    } else {
        used_memory_ = 0;
    }

    remove_expiry(key);
    lru_tracker_.remove(key);
    lfu_tracker_.remove(key);
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
        metrics_.record_miss();
        return false;
    }
    bool found = (data_.find(key) != data_.end());
    if (found) {
        metrics_.record_hit();
        lru_tracker_.touch(key);
        lfu_tracker_.touch(key);
    } else {
        metrics_.record_miss();
    }
    return found;
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

bool Store::set_maxmemory(size_t bytes) {
    maxmemory_ = bytes;
    if (maxmemory_ > 0 && used_memory_ > maxmemory_) {
        evict_keys_if_needed(0);
    }
    return true;
}

size_t Store::get_maxmemory() const noexcept {
    return maxmemory_;
}

bool Store::set_maxmemory_policy(std::string_view policy_str) {
    if (policy_str == "noeviction") {
        eviction_policy_ = EvictionPolicy::NOEVICTION;
    } else if (policy_str == "allkeys-lru") {
        eviction_policy_ = EvictionPolicy::ALLKEYS_LRU;
    } else if (policy_str == "allkeys-lfu") {
        eviction_policy_ = EvictionPolicy::ALLKEYS_LFU;
    } else {
        return false;
    }
    return true;
}

std::string Store::get_maxmemory_policy() const {
    switch (eviction_policy_) {
        case EvictionPolicy::NOEVICTION:
            return "noeviction";
        case EvictionPolicy::ALLKEYS_LRU:
            return "allkeys-lru";
        case EvictionPolicy::ALLKEYS_LFU:
            return "allkeys-lfu";
    }
    return "noeviction";
}

EvictionPolicy Store::get_eviction_policy_enum() const noexcept {
    return eviction_policy_;
}

size_t Store::used_memory() const noexcept {
    return used_memory_;
}

std::optional<size_t> Store::memory_usage(const std::string& key) {
    if (check_and_expire(key)) {
        return std::nullopt;
    }
    auto it = data_.find(key);
    if (it == data_.end()) {
        return std::nullopt;
    }
    return estimate_memory(key, it->second.value);
}

const EvictionRingBuffer& Store::eviction_buffer() const noexcept {
    return eviction_buffer_;
}

int64_t Store::ttl(const std::string& key) {
    int64_t ms = pttl(key);
    if (ms < 0) {
        return ms;
    }
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

bool Store::pexpireat(const std::string& key, int64_t epoch_ms, int64_t now_epoch_ms) {
    if (check_and_expire(key)) {
        return false;
    }
    auto it = data_.find(key);
    if (it == data_.end()) {
        return false;
    }

    int64_t current_ms = (now_epoch_ms != 0) ? now_epoch_ms : current_time_ms();
    int64_t diff = epoch_ms - current_ms;
    if (diff <= 0) {
        del(key);
        return true;
    }
    return pexpire(key, std::chrono::milliseconds(diff));
}

bool Store::expireat(const std::string& key, int64_t epoch_sec, int64_t now_epoch_sec) {
    return pexpireat(key, epoch_sec * 1000, now_epoch_sec * 1000);
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

        for (const auto& key : sample) {
            stats.sampled_keys++;
            auto it = data_.find(key);
            if (it != data_.end() && is_expired(it->second)) {
                size_t mem = estimate_memory(key, it->second.value);
                if (used_memory_ >= mem) {
                    used_memory_ -= mem;
                } else {
                    used_memory_ = 0;
                }

                metrics_.record_expired(1);
                telemetry_buffer_.push(TelemetryEvent{current_time_ms(), TelemetryEventType::EXPIRATION, key, "active_cycle"});

                remove_expiry(key);
                lru_tracker_.remove(key);
                lfu_tracker_.remove(key);
                data_.erase(it);
                ++stats.purged_keys;
                ++expired_in_iteration;
            } else if (it == data_.end()) {
                remove_expiry(key);
                lru_tracker_.remove(key);
                lfu_tracker_.remove(key);
            }
        }

        if (current_time() - start_time >= time_budget) {
            stats.timelimit_reached = true;
            break;
        }

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
    lru_tracker_.clear();
    lfu_tracker_.clear();
    eviction_buffer_.clear();
    telemetry_buffer_.clear();
    metrics_.reset();
    used_memory_ = 0;
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
            size_t mem = estimate_memory(it->first, it->second.value);
            if (used_memory_ >= mem) {
                used_memory_ -= mem;
            } else {
                used_memory_ = 0;
            }
            remove_expiry(it->first);
            lru_tracker_.remove(it->first);
            lfu_tracker_.remove(it->first);
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
