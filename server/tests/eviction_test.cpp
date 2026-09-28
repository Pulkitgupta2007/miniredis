#include "miniredis/eviction_ring_buffer.h"
#include "miniredis/lfu_tracker.h"
#include "miniredis/lru_tracker.h"
#include "miniredis/store.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <random>
#include <string>
#include <vector>

using namespace miniredis;
using namespace std::chrono_literals;

// -------------------------------------------------------------
// 1. Exact LRU Eviction Order Tests
// -------------------------------------------------------------
TEST(LruTrackerTest, ExactAccessSequenceEviction) {
    LruTracker lru;

    lru.add("A");
    lru.add("B");
    lru.add("C");
    // MRU -> C, B, A -> LRU

    // Touch A: now A is MRU
    // MRU -> A, C, B -> LRU
    lru.touch("A");

    // Touch C: now C is MRU
    // MRU -> C, A, B -> LRU
    lru.touch("C");

    // First eviction must be B
    EXPECT_EQ(lru.pop_lru(), "B");
    // Next eviction must be A
    EXPECT_EQ(lru.pop_lru(), "A");
    // Final eviction must be C
    EXPECT_EQ(lru.pop_lru(), "C");
    // Then empty
    EXPECT_EQ(lru.pop_lru(), std::nullopt);
}

// -------------------------------------------------------------
// 2. Exact LFU Eviction Order Tests
// -------------------------------------------------------------
TEST(LfuTrackerTest, ExactFrequencyEviction) {
    LfuTracker lfu;

    lfu.add("A"); // freq 1
    lfu.add("B"); // freq 1
    lfu.add("C"); // freq 1

    // A accessed 3 more times -> freq 4
    lfu.touch("A");
    lfu.touch("A");
    lfu.touch("A");

    // B accessed 1 more time -> freq 2
    lfu.touch("B");

    // C remains at freq 1

    // Lowest frequency is C (freq 1)
    EXPECT_EQ(lfu.pop_lfu(), "C");
    // Next lowest is B (freq 2)
    EXPECT_EQ(lfu.pop_lfu(), "B");
    // Highest is A (freq 4)
    EXPECT_EQ(lfu.pop_lfu(), "A");
    EXPECT_EQ(lfu.pop_lfu(), std::nullopt);
}

// -------------------------------------------------------------
// 3. LFU Tie-Breaking (LRU within same frequency bucket)
// -------------------------------------------------------------
TEST(LfuTrackerTest, TieBreakingUnderSameFrequency) {
    LfuTracker lfu;

    // Insert X, Y, Z with initial frequency 1
    lfu.add("X");
    lfu.add("Y");
    lfu.add("Z");
    // Bucket[1] order (MRU -> LRU): Z, Y, X

    // Touch X: moves to Bucket[2]
    lfu.touch("X");
    // Touch Y: moves to Bucket[2]
    lfu.touch("Y");
    // Bucket[2] order (MRU -> LRU): Y, X

    // Both X and Y have frequency 2, but X was touched earlier than Y.
    // Evicting from Bucket[2] must pick X (the least recently used) before Y!
    lfu.remove("Z"); // Remove the freq 1 element
    EXPECT_EQ(lfu.pop_lfu(), "X");
    EXPECT_EQ(lfu.pop_lfu(), "Y");
}

// -------------------------------------------------------------
// 4. Memory Accounting Integrity: DEL, Overwrite, Expiry
// -------------------------------------------------------------
TEST(StoreEvictionTest, MemoryAccountingIntegrity) {
    Store::TimePoint mock_time(1000s);
    Store store([&mock_time]() { return mock_time; });

    EXPECT_EQ(store.used_memory(), 0u);

    // 1. Initial SET: memory increases
    store.set("key1", "val1");
    size_t mem1 = store.used_memory();
    EXPECT_GT(mem1, 0u);

    // 2. Overwrite with larger value: memory increases by delta
    store.set("key1", "val1_much_longer_string_payload");
    size_t mem2 = store.used_memory();
    EXPECT_GT(mem2, mem1);

    // 3. Overwrite with shorter value: memory decreases by delta
    store.set("key1", "v");
    size_t mem3 = store.used_memory();
    EXPECT_LT(mem3, mem2);

    // 4. DEL removes key and restores memory to zero
    store.del("key1");
    EXPECT_EQ(store.used_memory(), 0u);

    // 5. Expiry removes memory upon passive expiration
    store.set("temp", "expiring_data", 500ms);
    EXPECT_GT(store.used_memory(), 0u);
    mock_time += 600ms;
    // Trigger passive expiry
    EXPECT_FALSE(store.get("temp").has_value());
    EXPECT_EQ(store.used_memory(), 0u);
}

// -------------------------------------------------------------
// 5. Store Policy Enforcement: noeviction returns OOM
// -------------------------------------------------------------
TEST(StoreEvictionTest, NoEvictionReturnsOOM) {
    Store store;
    store.set_maxmemory_policy("noeviction");

    // First key: 4 + 4 + 64 = 72 bytes
    store.set("k001", "v001");
    size_t key_size = store.used_memory();

    // Cap maxmemory to hold exactly 1 key
    store.set_maxmemory(key_size);

    // Inserting a second key must fail with OOM
    SetResult res = store.set("k002", "v002");
    EXPECT_FALSE(res.ok);
    EXPECT_TRUE(res.oom);
    EXPECT_EQ(store.dbsize(), 1u);
}

// -------------------------------------------------------------
// 6. Store Policy Enforcement: allkeys-lru evicts and emits to ring buffer
// -------------------------------------------------------------
TEST(StoreEvictionTest, AllKeysLruEvictionAndRingBuffer) {
    Store store;
    store.set_maxmemory_policy("allkeys-lru");

    // Insert two keys
    store.set("first", "data1");
    store.set("second", "data2");
    size_t two_keys_mem = store.used_memory();

    // Set maxmemory to hold only two keys
    store.set_maxmemory(two_keys_mem);

    // Touch 'first' so 'second' becomes the LRU victim
    store.get("first");

    // Insert third key -> triggers eviction of 'second'
    SetResult res = store.set("third", "data3");
    EXPECT_TRUE(res.ok);
    EXPECT_FALSE(res.oom);

    EXPECT_FALSE(store.exists("second")); // Evicted!
    EXPECT_TRUE(store.exists("first"));
    EXPECT_TRUE(store.exists("third"));

    // Verify eviction ring buffer telemetry
    const auto& ring = store.eviction_buffer();
    EXPECT_EQ(ring.total_evictions(), 1u);
    auto recent = ring.get_recent(5);
    ASSERT_EQ(recent.size(), 1u);
    EXPECT_EQ(recent[0].key, "second");
    EXPECT_EQ(recent[0].policy, "allkeys-lru");
    EXPECT_GT(recent[0].freed_bytes, 0u);
}

// -------------------------------------------------------------
// 7. Store Policy Enforcement: allkeys-lfu evicts least frequent
// -------------------------------------------------------------
TEST(StoreEvictionTest, AllKeysLfuEviction) {
    Store store;
    store.set_maxmemory_policy("allkeys-lfu");

    store.set("A", "val");
    store.set("B", "val");
    size_t two_keys_mem = store.used_memory();
    store.set_maxmemory(two_keys_mem);

    // Boost frequency of A by reading it 5 times
    for (int i = 0; i < 5; ++i) {
        store.get("A");
    }

    // Insert C: B (frequency 1) must be evicted instead of A (frequency 6)
    SetResult res = store.set("C", "val");
    EXPECT_TRUE(res.ok);
    EXPECT_TRUE(store.exists("A"));
    EXPECT_FALSE(store.exists("B"));
    EXPECT_TRUE(store.exists("C"));
}

// -------------------------------------------------------------
// 8. Property-Style Randomized Test: O(1) LRU vs Naive O(N) Reference
// -------------------------------------------------------------

class NaiveLruReference {
    struct Item {
        std::string key;
        int64_t access_seq;
    };
    std::vector<Item> items_;
    int64_t clock_seq_{0};
    size_t capacity_;

public:
    explicit NaiveLruReference(size_t cap) : capacity_(cap) {}

    std::optional<std::string> set(const std::string& key) {
        ++clock_seq_;
        for (auto& item : items_) {
            if (item.key == key) {
                item.access_seq = clock_seq_;
                return std::nullopt;
            }
        }
        std::optional<std::string> evicted = std::nullopt;
        if (items_.size() >= capacity_) {
            // Find minimum access_seq via linear O(N) scan
            auto min_it = std::min_element(items_.begin(), items_.end(),
                [](const Item& a, const Item& b) { return a.access_seq < b.access_seq; });
            evicted = min_it->key;
            items_.erase(min_it);
        }
        items_.push_back({key, clock_seq_});
        return evicted;
    }

    void touch(const std::string& key) {
        ++clock_seq_;
        for (auto& item : items_) {
            if (item.key == key) {
                item.access_seq = clock_seq_;
                break;
            }
        }
    }
};

TEST(LruPropertyTest, RandomizedComparisonAgainstNaiveReference) {
    constexpr size_t kCapacity = 5;
    constexpr size_t kTotalOperations = 600;

    LruTracker custom_lru;
    NaiveLruReference naive_ref(kCapacity);

    std::mt19937 rng(999);
    std::uniform_int_distribution<int> op_dist(0, 1);      // 0 = set, 1 = touch
    std::uniform_int_distribution<int> key_dist(1, 15);    // 15 distinct keys

    for (size_t op = 0; op < kTotalOperations; ++op) {
        std::string key = "key_" + std::to_string(key_dist(rng));
        int action = op_dist(rng);

        if (action == 0) { // SET
            std::optional<std::string> naive_evicted = naive_ref.set(key);
            std::optional<std::string> custom_evicted = std::nullopt;

            if (custom_lru.size() >= kCapacity) {
                // Check if key is already in custom_lru
                // If not in custom_lru, one element must be evicted
                custom_lru.add(key);
                if (custom_lru.size() > kCapacity) {
                    custom_evicted = custom_lru.pop_lru();
                }
            } else {
                custom_lru.add(key);
            }

            // Both implementations must agree on whether an eviction occurred and on the exact key
            EXPECT_EQ(custom_evicted, naive_evicted)
                << "Mismatch at step " << op << " for key " << key;
        } else { // TOUCH
            naive_ref.touch(key);
            custom_lru.touch(key);
        }
    }
}
