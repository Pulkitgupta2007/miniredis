#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace miniredis {

// Custom O(1) LRU Tracker: Hash map + Doubly Linked List with Sentinel Head/Tail
class LruTracker {
public:
    LruTracker() {
        head_ = new Node();
        tail_ = new Node();
        head_->next = tail_;
        tail_->prev = head_;
    }

    ~LruTracker() {
        clear();
        delete head_;
        delete tail_;
    }

    LruTracker(const LruTracker&) = delete;
    LruTracker& operator=(const LruTracker&) = delete;

    size_t size() const noexcept {
        return map_.size();
    }

    bool empty() const noexcept {
        return map_.empty();
    }

    // Records access to a key, moving it to the MRU position (head)
    void touch(const std::string& key) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            detach(it->second);
            attach_head(it->second);
        }
    }

    // Inserts a new key as MRU, or moves to MRU if already present
    void add(const std::string& key) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            detach(it->second);
            attach_head(it->second);
            return;
        }

        Node* node = new Node(key);
        attach_head(node);
        map_[key] = node;
    }

    // Removes a key from the tracker
    bool remove(const std::string& key) {
        auto it = map_.find(key);
        if (it == map_.end()) {
            return false;
        }
        Node* node = it->second;
        detach(node);
        map_.erase(it);
        delete node;
        return true;
    }

    // Pops and returns the least recently used (LRU) key (tail) in O(1)
    std::optional<std::string> pop_lru() {
        if (empty()) {
            return std::nullopt;
        }

        Node* lru_node = tail_->prev;
        std::string key = lru_node->key;
        detach(lru_node);
        map_.erase(key);
        delete lru_node;
        return key;
    }

    void clear() {
        Node* curr = head_->next;
        while (curr != tail_) {
            Node* nxt = curr->next;
            delete curr;
            curr = nxt;
        }
        head_->next = tail_;
        tail_->prev = head_;
        map_.clear();
    }

private:
    struct Node {
        std::string key{};
        Node* prev{nullptr};
        Node* next{nullptr};

        Node() = default;
        explicit Node(std::string k) : key(std::move(k)) {}
    };

    void attach_head(Node* node) {
        node->next = head_->next;
        node->prev = head_;
        head_->next->prev = node;
        head_->next = node;
    }

    void detach(Node* node) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }

    Node* head_{nullptr}; // Sentinel head: head_->next is MRU
    Node* tail_{nullptr}; // Sentinel tail: tail_->prev is LRU
    std::unordered_map<std::string, Node*> map_;
};

} // namespace miniredis
