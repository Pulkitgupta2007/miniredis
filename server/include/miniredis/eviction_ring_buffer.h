#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace miniredis {

struct EvictionEvent {
    std::string key;
    std::string policy;
    int64_t timestamp_ms{0};
    size_t freed_bytes{0};
};

class EvictionRingBuffer {
public:
    explicit EvictionRingBuffer(size_t capacity = 256)
        : capacity_(capacity > 0 ? capacity : 1), buffer_(capacity_), head_(0), size_(0), total_evictions_(0) {}

    void push(EvictionEvent ev) {
        buffer_[head_] = std::move(ev);
        head_ = (head_ + 1) % capacity_;
        if (size_ < capacity_) {
            ++size_;
        }
        ++total_evictions_;
    }

    std::vector<EvictionEvent> get_recent(size_t limit = 50) const {
        std::vector<EvictionEvent> out;
        size_t count = std::min(limit, size_);
        out.reserve(count);

        // Read backwards from newest to oldest
        for (size_t i = 0; i < count; ++i) {
            size_t idx = (head_ + capacity_ - 1 - i) % capacity_;
            out.push_back(buffer_[idx]);
        }
        return out;
    }

    size_t total_evictions() const noexcept {
        return total_evictions_;
    }

    void clear() {
        head_ = 0;
        size_ = 0;
        total_evictions_ = 0;
    }

private:
    size_t capacity_;
    std::vector<EvictionEvent> buffer_;
    size_t head_;
    size_t size_;
    size_t total_evictions_;
};

} // namespace miniredis
