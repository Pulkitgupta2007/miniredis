#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace miniredis {

// Custom O(1) LFU Tracker:
// - Map of frequency -> Doubly Linked List of keys (LRU tie-breaking within same frequency tier)
// - Map of key -> Node pointer
// - min_freq_ pointer for O(1) eviction
class LfuTracker {
public:
    LfuTracker() : min_freq_(0) {}

    ~LfuTracker() {
        clear();
    }

    LfuTracker(const LfuTracker&) = delete;
    LfuTracker& operator=(const LfuTracker&) = delete;

    size_t size() const noexcept {
        return key_map_.size();
    }

    bool empty() const noexcept {
        return key_map_.empty();
    }

    // Inserts a new key with frequency 1, or increments frequency if already present
    void add(const std::string& key) {
        auto it = key_map_.find(key);
        if (it != key_map_.end()) {
            touch(key);
            return;
        }

        Node* node = new Node(key, 1);
        key_map_[key] = node;

        FreqBucket* bucket = get_or_create_bucket(1);
        bucket->attach_head(node);
        min_freq_ = 1;
    }

    // Increments frequency of existing key and moves it to the head of the new frequency bucket
    void touch(const std::string& key) {
        auto it = key_map_.find(key);
        if (it == key_map_.end()) {
            return;
        }

        Node* node = it->second;
        size_t old_freq = node->freq;
        FreqBucket* old_bucket = freq_buckets_[old_freq];
        old_bucket->detach(node);

        if (old_bucket->empty() && min_freq_ == old_freq) {
            min_freq_ = old_freq + 1;
        }

        node->freq = old_freq + 1;
        FreqBucket* new_bucket = get_or_create_bucket(node->freq);
        new_bucket->attach_head(node);
    }

    // Removes key from LFU structures
    bool remove(const std::string& key) {
        auto it = key_map_.find(key);
        if (it == key_map_.end()) {
            return false;
        }

        Node* node = it->second;
        FreqBucket* bucket = freq_buckets_[node->freq];
        bucket->detach(node);

        key_map_.erase(it);
        delete node;

        // If table empty, reset min_freq_
        if (key_map_.empty()) {
            min_freq_ = 0;
        }
        return true;
    }

    // Evicts and returns key with lowest frequency in O(1).
    // Tie-breaker: least recently used (tail of min_freq bucket).
    std::optional<std::string> pop_lfu() {
        if (key_map_.empty()) {
            return std::nullopt;
        }

        // Advance min_freq_ if the bucket was emptied by removals
        while (freq_buckets_.find(min_freq_) == freq_buckets_.end() || freq_buckets_[min_freq_]->empty()) {
            ++min_freq_;
        }

        FreqBucket* bucket = freq_buckets_[min_freq_];
        Node* lfu_node = bucket->pop_tail();
        if (!lfu_node) {
            return std::nullopt;
        }

        std::string key = lfu_node->key;
        key_map_.erase(key);
        delete lfu_node;

        if (key_map_.empty()) {
            min_freq_ = 0;
        }
        return key;
    }

    void clear() {
        for (auto& [freq, bucket] : freq_buckets_) {
            delete bucket;
        }
        freq_buckets_.clear();

        for (auto& [key, node] : key_map_) {
            delete node;
        }
        key_map_.clear();
        min_freq_ = 0;
    }

private:
    struct Node {
        std::string key{};
        size_t freq{1};
        Node* prev{nullptr};
        Node* next{nullptr};

        Node() = default;
        Node(std::string k, size_t f) : key(std::move(k)), freq(f) {}
    };

    struct FreqBucket {
        Node* head{nullptr};
        Node* tail{nullptr};
        size_t count{0};

        FreqBucket() {
            head = new Node();
            tail = new Node();
            head->next = tail;
            tail->prev = head;
        }

        ~FreqBucket() {
            delete head;
            delete tail;
        }

        bool empty() const noexcept {
            return count == 0;
        }

        void attach_head(Node* node) {
            node->next = head->next;
            node->prev = head;
            head->next->prev = node;
            head->next = node;
            ++count;
        }

        void detach(Node* node) {
            node->prev->next = node->next;
            node->next->prev = node->prev;
            --count;
        }

        Node* pop_tail() {
            if (empty()) {
                return nullptr;
            }
            Node* lru_in_bucket = tail->prev;
            detach(lru_in_bucket);
            return lru_in_bucket;
        }
    };

    FreqBucket* get_or_create_bucket(size_t freq) {
        auto it = freq_buckets_.find(freq);
        if (it == freq_buckets_.end()) {
            FreqBucket* b = new FreqBucket();
            freq_buckets_[freq] = b;
            return b;
        }
        return it->second;
    }

    size_t min_freq_;
    std::unordered_map<size_t, FreqBucket*> freq_buckets_;
    std::unordered_map<std::string, Node*> key_map_;
};

} // namespace miniredis
