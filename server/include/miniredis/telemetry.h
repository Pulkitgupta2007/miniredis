#pragma once

#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace miniredis {

enum class TelemetryEventType {
    EVICTION,
    EXPIRATION,
    COMMAND
};

inline const char* to_string(TelemetryEventType t) noexcept {
    switch (t) {
        case TelemetryEventType::EVICTION:
            return "EVICTION";
        case TelemetryEventType::EXPIRATION:
            return "EXPIRATION";
        case TelemetryEventType::COMMAND:
            return "COMMAND";
    }
    return "UNKNOWN";
}

struct TelemetryEvent {
    int64_t timestamp_ms{0};
    TelemetryEventType type{TelemetryEventType::COMMAND};
    std::string key{};
    std::string details{};

    std::string format() const {
        std::ostringstream oss;
        oss << timestamp_ms << " " << to_string(type) << " " << key;
        if (!details.empty()) {
            oss << " " << details;
        }
        return oss.str();
    }
};

class TelemetryRingBuffer {
public:
    explicit TelemetryRingBuffer(size_t capacity = 512)
        : capacity_(capacity > 0 ? capacity : 1), buffer_(capacity_), head_(0), size_(0), total_events_(0) {}

    void push(TelemetryEvent ev) {
        buffer_[head_] = std::move(ev);
        head_ = (head_ + 1) % capacity_;
        if (size_ < capacity_) {
            ++size_;
        }
        ++total_events_;
    }

    std::vector<TelemetryEvent> get_recent(size_t limit = 50) const {
        std::vector<TelemetryEvent> out;
        size_t count = std::min(limit, size_);
        out.reserve(count);

        // Read newest to oldest
        for (size_t i = 0; i < count; ++i) {
            size_t idx = (head_ + capacity_ - 1 - i) % capacity_;
            out.push_back(buffer_[idx]);
        }
        return out;
    }

    size_t total_events() const noexcept {
        return total_events_;
    }

    void clear() {
        head_ = 0;
        size_ = 0;
        total_events_ = 0;
    }

private:
    size_t capacity_;
    std::vector<TelemetryEvent> buffer_;
    size_t head_;
    size_t size_;
    size_t total_events_;
};

class ServerMetrics {
public:
    ServerMetrics()
        : start_time_(std::chrono::steady_clock::now()),
          last_sample_time_(start_time_),
          last_sample_commands_(0),
          cached_ops_per_sec_(0.0) {}

    void record_hit() noexcept { ++keyspace_hits_; }
    void record_miss() noexcept { ++keyspace_misses_; }
    void record_expired(size_t count = 1) noexcept { expired_keys_ += count; }
    void record_command() noexcept { ++total_commands_processed_; }

    size_t keyspace_hits() const noexcept { return keyspace_hits_; }
    size_t keyspace_misses() const noexcept { return keyspace_misses_; }
    size_t expired_keys() const noexcept { return expired_keys_; }
    size_t total_commands_processed() const noexcept { return total_commands_processed_; }

    double hit_ratio() const noexcept {
        size_t total = keyspace_hits_ + keyspace_misses_;
        if (total == 0) {
            return 0.0;
        }
        return static_cast<double>(keyspace_hits_) / static_cast<double>(total);
    }

    int64_t uptime_seconds(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const noexcept {
        auto diff = now - start_time_;
        return std::chrono::duration_cast<std::chrono::seconds>(diff).count();
    }

    double ops_per_sec(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const {
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_sample_time_).count();
        if (elapsed_ms >= 500) { // Refresh sample every 500ms
            size_t cmd_delta = total_commands_processed_ - last_sample_commands_;
            cached_ops_per_sec_ = (static_cast<double>(cmd_delta) * 1000.0) / static_cast<double>(elapsed_ms);
            last_sample_time_ = now;
            last_sample_commands_ = total_commands_processed_;
        } else if (last_sample_commands_ == 0) {
            int64_t up = uptime_seconds(now);
            if (up > 0) {
                cached_ops_per_sec_ = static_cast<double>(total_commands_processed_) / static_cast<double>(up);
            } else {
                cached_ops_per_sec_ = static_cast<double>(total_commands_processed_);
            }
        }
        return cached_ops_per_sec_;
    }

    void reset() {
        keyspace_hits_ = 0;
        keyspace_misses_ = 0;
        expired_keys_ = 0;
        total_commands_processed_ = 0;
        start_time_ = std::chrono::steady_clock::now();
        last_sample_time_ = start_time_;
        last_sample_commands_ = 0;
        cached_ops_per_sec_ = 0.0;
    }

private:
    size_t keyspace_hits_{0};
    size_t keyspace_misses_{0};
    size_t expired_keys_{0};
    size_t total_commands_processed_{0};

    std::chrono::steady_clock::time_point start_time_;
    mutable std::chrono::steady_clock::time_point last_sample_time_;
    mutable size_t last_sample_commands_{0};
    mutable double cached_ops_per_sec_{0.0};
};

} // namespace miniredis
