#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace miniredis {

// Stretch Goal: Custom Chained Hash Table with dynamic resizing
// Uses separate chaining with singly linked lists and power-of-two bucket array.
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class ChainedHashTable {
public:
    explicit ChainedHashTable(size_t initial_buckets = 16, float max_load_factor = 1.0f)
        : max_load_factor_(max_load_factor), size_(0) {
        buckets_.resize(std::max<size_t>(initial_buckets, 4));
    }

    size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    size_t bucket_count() const noexcept { return buckets_.size(); }
    float load_factor() const noexcept {
        return static_cast<float>(size_) / static_cast<float>(buckets_.size());
    }

    // Insert or update. Returns true if inserted as new key, false if updated existing key.
    bool insert_or_assign(const Key& key, Value value) {
        if (load_factor() >= max_load_factor_) {
            rehash(buckets_.size() * 2);
        }

        size_t idx = bucket_index(key, buckets_.size());
        Node* curr = buckets_[idx].get();
        while (curr) {
            if (curr->key == key) {
                curr->value = std::move(value);
                return false; // Updated existing
            }
            curr = curr->next.get();
        }

        // Insert at head of bucket chain
        auto new_node = std::make_unique<Node>(key, std::move(value), std::move(buckets_[idx]));
        buckets_[idx] = std::move(new_node);
        ++size_;
        return true;
    }

    const Value* find(const Key& key) const {
        size_t idx = bucket_index(key, buckets_.size());
        Node* curr = buckets_[idx].get();
        while (curr) {
            if (curr->key == key) {
                return &curr->value;
            }
            curr = curr->next.get();
        }
        return nullptr;
    }

    Value* find(const Key& key) {
        size_t idx = bucket_index(key, buckets_.size());
        Node* curr = buckets_[idx].get();
        while (curr) {
            if (curr->key == key) {
                return &curr->value;
            }
            curr = curr->next.get();
        }
        return nullptr;
    }

    bool erase(const Key& key) {
        size_t idx = bucket_index(key, buckets_.size());
        Node* curr = buckets_[idx].get();
        Node* prev = nullptr;

        while (curr) {
            if (curr->key == key) {
                if (prev) {
                    prev->next = std::move(curr->next);
                } else {
                    buckets_[idx] = std::move(curr->next);
                }
                --size_;
                return true;
            }
            prev = curr;
            curr = curr->next.get();
        }
        return false;
    }

    void clear() {
        for (auto& bucket : buckets_) {
            bucket.reset();
        }
        size_ = 0;
    }

private:
    struct Node {
        Key key;
        Value value;
        std::unique_ptr<Node> next;

        Node(Key k, Value v, std::unique_ptr<Node> n)
            : key(std::move(k)), value(std::move(v)), next(std::move(n)) {}
    };

    size_t bucket_index(const Key& key, size_t num_buckets) const {
        return hash_fn_(key) % num_buckets;
    }

    void rehash(size_t new_bucket_count) {
        std::vector<std::unique_ptr<Node>> new_buckets(new_bucket_count);

        for (auto& bucket : buckets_) {
            std::unique_ptr<Node> curr = std::move(bucket);
            while (curr) {
                std::unique_ptr<Node> next = std::move(curr->next);
                size_t new_idx = bucket_index(curr->key, new_bucket_count);

                curr->next = std::move(new_buckets[new_idx]);
                new_buckets[new_idx] = std::move(curr);

                curr = std::move(next);
            }
        }

        buckets_ = std::move(new_buckets);
    }

    Hash hash_fn_;
    float max_load_factor_;
    size_t size_;
    std::vector<std::unique_ptr<Node>> buckets_;
};

} // namespace miniredis
