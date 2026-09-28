#include "miniredis/store.h"
#include "miniredis/chained_hash_table.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using namespace miniredis;
using namespace std::chrono_literals;

class StoreTest : public ::testing::Test {
protected:
    Store::TimePoint mock_now = Store::TimePoint(10'000s);

    Store create_mocked_store() {
        return Store([this]() { return mock_now; });
    }

    void advance_time(std::chrono::milliseconds ms) {
        mock_now += ms;
    }
};

TEST_F(StoreTest, BasicSetAndGet) {
    Store store = create_mocked_store();

    EXPECT_TRUE(store.set("user:1", "alice"));
    auto val = store.get("user:1");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "alice");
}

TEST_F(StoreTest, MissingKey) {
    Store store = create_mocked_store();

    EXPECT_FALSE(store.get("nonexistent_key").has_value());
    EXPECT_FALSE(store.exists("nonexistent_key"));
    EXPECT_EQ(store.ttl("nonexistent_key"), -2);
    EXPECT_EQ(store.pttl("nonexistent_key"), -2);
}

TEST_F(StoreTest, OverwriteValue) {
    Store store = create_mocked_store();

    EXPECT_TRUE(store.set("key", "val1"));
    EXPECT_EQ(store.get("key"), "val1");

    EXPECT_TRUE(store.set("key", "val2"));
    EXPECT_EQ(store.get("key"), "val2");
    EXPECT_EQ(store.dbsize(), 1u);
}

TEST_F(StoreTest, DeleteMissingKey) {
    Store store = create_mocked_store();

    EXPECT_FALSE(store.del("no_such_key"));
    EXPECT_EQ(store.dbsize(), 0u);

    store.set("real_key", "value");
    EXPECT_EQ(store.del(std::vector<std::string>{"no_such_key", "real_key", "another_missing"}), 1u);
    EXPECT_EQ(store.dbsize(), 0u);
}

TEST_F(StoreTest, EmptyAndLargeValues) {
    Store store = create_mocked_store();

    EXPECT_TRUE(store.set("", ""));
    auto empty_val = store.get("");
    ASSERT_TRUE(empty_val.has_value());
    EXPECT_EQ(*empty_val, "");
    EXPECT_TRUE(store.del(""));

    const size_t payload_size = 5 * 1024 * 1024;
    std::string large_val(payload_size, 'X');
    EXPECT_TRUE(store.set("large_key", large_val));

    auto retrieved_large = store.get("large_key");
    ASSERT_TRUE(retrieved_large.has_value());
    EXPECT_EQ(retrieved_large->size(), payload_size);
    EXPECT_EQ(*retrieved_large, large_val);
}

TEST_F(StoreTest, BinarySafeKeysAndValues) {
    Store store = create_mocked_store();

    std::string binary_key("key\0with\0nulls", 14);
    std::string binary_value("val\0\r\n\x01\x02\xFF\0end", 12);

    EXPECT_TRUE(store.set(binary_key, binary_value));
    EXPECT_TRUE(store.exists(binary_key));

    auto retrieved = store.get(binary_key);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->size(), 12u);
    EXPECT_EQ(*retrieved, binary_value);

    EXPECT_TRUE(store.del(binary_key));
    EXPECT_FALSE(store.exists(binary_key));
}

TEST_F(StoreTest, PingCommand) {
    Store store = create_mocked_store();

    EXPECT_EQ(store.ping(), "PONG");
    EXPECT_EQ(store.ping("hello world"), "hello world");
    EXPECT_EQ(store.ping(""), "");
}

TEST_F(StoreTest, FlushAll) {
    Store store = create_mocked_store();

    store.set("k1", "v1");
    store.set("k2", "v2", 5000ms);
    EXPECT_EQ(store.dbsize(), 2u);
    EXPECT_EQ(store.expires_size(), 1u);

    store.flushall();
    EXPECT_EQ(store.dbsize(), 0u);
    EXPECT_EQ(store.expires_size(), 0u);
    EXPECT_FALSE(store.exists("k1"));
    EXPECT_FALSE(store.exists("k2"));
}

TEST_F(StoreTest, SetWithConditions) {
    Store store = create_mocked_store();

    EXPECT_TRUE(store.set("nx_key", "first", std::nullopt, SetCondition::NX));
    EXPECT_FALSE(store.set("nx_key", "second", std::nullopt, SetCondition::NX));
    EXPECT_EQ(store.get("nx_key"), "first");

    EXPECT_FALSE(store.set("xx_key", "val", std::nullopt, SetCondition::XX));
    EXPECT_FALSE(store.exists("xx_key"));

    store.set("xx_key", "initial");
    EXPECT_TRUE(store.set("xx_key", "updated", std::nullopt, SetCondition::XX));
    EXPECT_EQ(store.get("xx_key"), "updated");
}

TEST_F(StoreTest, DelSingleAndMultiple) {
    Store store = create_mocked_store();

    store.set("k1", "v1");
    store.set("k2", "v2");
    store.set("k3", "v3");

    EXPECT_TRUE(store.del("k1"));
    EXPECT_FALSE(store.del("k1"));
    EXPECT_FALSE(store.exists("k1"));

    EXPECT_EQ(store.del(std::vector<std::string>{"k2", "k3", "k4"}), 2u);
    EXPECT_FALSE(store.exists("k2"));
    EXPECT_FALSE(store.exists("k3"));
}

TEST_F(StoreTest, ExistsSingleAndMultiple) {
    Store store = create_mocked_store();

    store.set("a", "1");
    store.set("b", "2");

    EXPECT_TRUE(store.exists("a"));
    EXPECT_FALSE(store.exists("c"));

    EXPECT_EQ(store.exists(std::vector<std::string>{"a", "b", "c", "d"}), 2u);
}

// -------------------------------------------------------------
// Phase 3 Specific: TTL & Expiry with Mock Clock
// -------------------------------------------------------------

TEST_F(StoreTest, TTLAndPTTLCalculations) {
    Store store = create_mocked_store();

    EXPECT_EQ(store.ttl("missing"), -2);
    EXPECT_EQ(store.pttl("missing"), -2);

    store.set("persistent", "hello");
    EXPECT_EQ(store.ttl("persistent"), -1);
    EXPECT_EQ(store.pttl("persistent"), -1);

    store.set("temp", "world", 5000ms);
    EXPECT_EQ(store.pttl("temp"), 5000);
    EXPECT_EQ(store.ttl("temp"), 5);

    // Advance mock time without real sleep
    advance_time(2000ms);
    EXPECT_EQ(store.pttl("temp"), 3000);
    EXPECT_EQ(store.ttl("temp"), 3);
}

TEST_F(StoreTest, LazyExpiryOnAccess) {
    Store store = create_mocked_store();

    store.set("session", "token_123", 1000ms);
    EXPECT_TRUE(store.exists("session"));
    EXPECT_EQ(store.expires_size(), 1u);

    // Advance clock past expiration
    advance_time(1001ms);

    // Lazy expiry triggered by get()
    EXPECT_FALSE(store.get("session").has_value());
    EXPECT_EQ(store.dbsize(), 0u);
    EXPECT_EQ(store.expires_size(), 0u);

    // Re-insert and test lazy expiry via exists()
    store.set("session2", "token_456", 500ms);
    advance_time(501ms);
    EXPECT_FALSE(store.exists("session2"));
    EXPECT_EQ(store.dbsize(), 0u);

    // Re-insert and test lazy expiry via ttl()
    store.set("session3", "token_789", 500ms);
    advance_time(501ms);
    EXPECT_EQ(store.ttl("session3"), -2);
    EXPECT_EQ(store.dbsize(), 0u);
}

TEST_F(StoreTest, ExpireAndPexpire) {
    Store store = create_mocked_store();

    store.set("item", "data");
    EXPECT_EQ(store.ttl("item"), -1);

    EXPECT_TRUE(store.expire("item", 10s));
    EXPECT_EQ(store.ttl("item"), 10);

    EXPECT_TRUE(store.pexpire("item", 2500ms));
    EXPECT_EQ(store.pttl("item"), 2500);

    EXPECT_FALSE(store.expire("unknown", 5s));
}

TEST_F(StoreTest, PersistClearsTTL) {
    Store store = create_mocked_store();

    store.set("token", "abc", 5000ms);
    EXPECT_EQ(store.pttl("token"), 5000);
    EXPECT_EQ(store.expires_size(), 1u);

    // PERSIST removes TTL and moves key from expiring to persistent
    EXPECT_TRUE(store.persist("token"));
    EXPECT_EQ(store.ttl("token"), -1);
    EXPECT_EQ(store.expires_size(), 0u);

    // Calling PERSIST on key without TTL returns false
    EXPECT_FALSE(store.persist("token"));

    // Advance time far into future; key must still persist
    advance_time(100'000ms);
    EXPECT_EQ(store.get("token"), "abc");
}

TEST_F(StoreTest, TTLBehaviorAfterOverwrite) {
    Store store = create_mocked_store();

    // 1. SET with TTL followed by standard SET without TTL clears the prior TTL
    store.set("k", "v1", 2000ms);
    EXPECT_EQ(store.expires_size(), 1u);
    EXPECT_EQ(store.ttl("k"), 2);

    store.set("k", "v2"); // Overwrite without TTL
    EXPECT_EQ(store.expires_size(), 0u);
    EXPECT_EQ(store.ttl("k"), -1);

    advance_time(3000ms);
    EXPECT_EQ(store.get("k"), "v2"); // Still alive!

    // 2. SET with TTL followed by another SET with new TTL updates expiration
    store.set("k", "v3", 10000ms);
    EXPECT_EQ(store.ttl("k"), 10);
    EXPECT_EQ(store.expires_size(), 1u);

    // 3. SET NX on existing key fails and does not alter previous TTL
    EXPECT_FALSE(store.set("k", "v4", 500ms, SetCondition::NX));
    EXPECT_EQ(store.ttl("k"), 10); // Unaltered
}

TEST_F(StoreTest, ZeroOrNegativeTTLDeletesKey) {
    Store store = create_mocked_store();

    store.set("k1", "v1");
    EXPECT_TRUE(store.expire("k1", 0s));
    EXPECT_FALSE(store.exists("k1"));

    store.set("k2", "v2", -100ms);
    EXPECT_FALSE(store.exists("k2"));
}

TEST_F(StoreTest, ActiveExpireRandomSamplingAndPurge) {
    Store store = create_mocked_store();

    // Create 100 keys with TTL
    for (int i = 0; i < 100; ++i) {
        store.set("exp_" + std::to_string(i), "val", 1000ms);
    }
    // Create 50 permanent keys
    for (int i = 0; i < 50; ++i) {
        store.set("perm_" + std::to_string(i), "val");
    }

    EXPECT_EQ(store.expires_size(), 100u);
    EXPECT_EQ(store.dbsize(), 150u);

    // Before expiration, active cycle should purge 0 keys
    auto stats_pre = store.active_expire_cycle(20);
    EXPECT_EQ(stats_pre.purged_keys, 0u);

    // Advance clock past expiration
    advance_time(1500ms);

    // Run active cycle: samples random keys without requiring GET calls
    auto stats_post = store.active_expire_cycle(20);
    EXPECT_GT(stats_post.purged_keys, 0u);
    EXPECT_EQ(store.expires_size(), 0u);
    EXPECT_EQ(store.dbsize(), 50u);

    // Verify all permanent keys remain intact
    for (int i = 0; i < 50; ++i) {
        EXPECT_TRUE(store.exists("perm_" + std::to_string(i)));
    }
}

TEST_F(StoreTest, ActiveExpireRepeatsWhenOver25PercentExpired) {
    Store store = create_mocked_store();

    // Insert 40 keys with TTL
    for (int i = 0; i < 40; ++i) {
        store.set("key_" + std::to_string(i), "v", 500ms);
    }

    advance_time(600ms); // All 40 keys are now expired

    // With sample size 20, 100% of sample is expired (> 25%).
    // The cycle must loop more than 1 iteration and purge all 40 keys.
    auto stats = store.active_expire_cycle(20);
    EXPECT_GT(stats.iterations, 1u);
    EXPECT_EQ(stats.purged_keys, 40u);
    EXPECT_EQ(store.dbsize(), 0u);
}

TEST_F(StoreTest, ActiveExpireStopsWhenUnder25PercentExpired) {
    Store store = create_mocked_store();

    // Insert 100 keys with long TTL (1 hour)
    for (int i = 0; i < 97; ++i) {
        store.set("long_" + std::to_string(i), "v", 3600s);
    }
    // Insert 3 keys that expire soon (100ms)
    for (int i = 0; i < 3; ++i) {
        store.set("short_" + std::to_string(i), "v", 100ms);
    }

    advance_time(200ms);

    // Sample 20 keys: at most 3 can be expired (3 / 20 = 15% <= 25%)
    // The adaptive algorithm must stop after 1 iteration.
    auto stats = store.active_expire_cycle(20);
    EXPECT_EQ(stats.iterations, 1u);
    EXPECT_FALSE(stats.timelimit_reached);
}

TEST_F(StoreTest, ActiveExpireTimeBudgetCeiling) {
    // Inject a simulated clock that steps forward by 10ms on every call
    int call_count = 0;
    Store store([&call_count]() {
        call_count++;
        return Store::TimePoint(std::chrono::milliseconds(call_count * 10));
    });

    // Populate keys with expired TTLs
    for (int i = 0; i < 200; ++i) {
        store.set("k_" + std::to_string(i), "v", 1ms);
    }

    // Run active expire with a strict 25ms budget
    auto stats = store.active_expire_cycle(10, 25ms);

    // Must halt due to time budget ceiling
    EXPECT_TRUE(stats.timelimit_reached);
}

TEST_F(StoreTest, DBSizeAndFlushDB) {
    Store store = create_mocked_store();

    EXPECT_EQ(store.dbsize(), 0u);
    store.set("k1", "v1");
    store.set("k2", "v2");
    EXPECT_EQ(store.dbsize(), 2u);

    store.flushdb();
    EXPECT_EQ(store.dbsize(), 0u);
    EXPECT_FALSE(store.exists("k1"));
    EXPECT_FALSE(store.exists("k2"));
}

TEST_F(StoreTest, KeysPatternMatching) {
    Store store = create_mocked_store();

    store.set("user:1", "u1");
    store.set("user:2", "u2");
    store.set("user:10", "u10");
    store.set("session:abc", "s1");
    store.set("temp", "t", 500ms);

    auto all = store.keys("*");
    EXPECT_EQ(all.size(), 5u);

    auto users = store.keys("user:*");
    EXPECT_EQ(users.size(), 3u);

    auto single_digit_users = store.keys("user:?");
    EXPECT_EQ(single_digit_users.size(), 2u);

    advance_time(600ms);
    auto after_expiry = store.keys("*");
    EXPECT_EQ(after_expiry.size(), 4u);
    auto found_temp = std::find(after_expiry.begin(), after_expiry.end(), "temp");
    EXPECT_EQ(found_temp, after_expiry.end());
}

// -------------------------------------------------------------
// Stretch Goal Tests: Custom ChainedHashTable
// -------------------------------------------------------------
TEST(ChainedHashTableTest, InsertAndFind) {
    ChainedHashTable<std::string, std::string> table(4, 1.0f);

    EXPECT_TRUE(table.empty());
    EXPECT_EQ(table.size(), 0u);

    EXPECT_TRUE(table.insert_or_assign("alpha", "100"));
    EXPECT_EQ(table.size(), 1u);

    const std::string* val = table.find("alpha");
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, "100");

    EXPECT_EQ(table.find("missing"), nullptr);
}

TEST(ChainedHashTableTest, OverwriteAndErase) {
    ChainedHashTable<std::string, std::string> table(4, 1.0f);

    EXPECT_TRUE(table.insert_or_assign("key", "val1"));
    EXPECT_FALSE(table.insert_or_assign("key", "val2"));
    EXPECT_EQ(table.size(), 1u);

    const std::string* val = table.find("key");
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, "val2");

    EXPECT_TRUE(table.erase("key"));
    EXPECT_FALSE(table.erase("key"));
    EXPECT_EQ(table.size(), 0u);
    EXPECT_EQ(table.find("key"), nullptr);
}

TEST(ChainedHashTableTest, DynamicRehashing) {
    ChainedHashTable<int, int> table(4, 1.0f);
    EXPECT_EQ(table.bucket_count(), 4u);

    for (int i = 0; i < 20; ++i) {
        table.insert_or_assign(i, i * 10);
    }

    EXPECT_EQ(table.size(), 20u);
    EXPECT_GT(table.bucket_count(), 4u);
    EXPECT_LE(table.load_factor(), 1.0f);

    for (int i = 0; i < 20; ++i) {
        int* v = table.find(i);
        ASSERT_NE(v, nullptr);
        EXPECT_EQ(*v, i * 10);
    }
}
