#include "miniredis/store.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>

using namespace miniredis;
using namespace std::chrono_literals;

class StoreTest : public ::testing::Test {
protected:
    // Simulated steady time starting at an arbitrary fixed baseline
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

    // Non-existent key
    EXPECT_FALSE(store.get("nonexistent").has_value());
}

TEST_F(StoreTest, OverwriteValue) {
    Store store = create_mocked_store();

    EXPECT_TRUE(store.set("key", "val1"));
    EXPECT_EQ(store.get("key"), "val1");

    EXPECT_TRUE(store.set("key", "val2"));
    EXPECT_EQ(store.get("key"), "val2");
}

TEST_F(StoreTest, SetWithConditions) {
    Store store = create_mocked_store();

    // NX: Set only if not exists
    EXPECT_TRUE(store.set("nx_key", "first", std::nullopt, SetCondition::NX));
    EXPECT_FALSE(store.set("nx_key", "second", std::nullopt, SetCondition::NX));
    EXPECT_EQ(store.get("nx_key"), "first");

    // XX: Set only if exists
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

    // Single DEL
    EXPECT_TRUE(store.del("k1"));
    EXPECT_FALSE(store.del("k1")); // Already deleted
    EXPECT_FALSE(store.exists("k1"));

    // Multi DEL
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

TEST_F(StoreTest, TTLAndPTTLCalculations) {
    Store store = create_mocked_store();

    // Non-existent key returns -2
    EXPECT_EQ(store.ttl("missing"), -2);
    EXPECT_EQ(store.pttl("missing"), -2);

    // Key without expiration returns -1
    store.set("persistent", "hello");
    EXPECT_EQ(store.ttl("persistent"), -1);
    EXPECT_EQ(store.pttl("persistent"), -1);

    // Key with 5000ms expiration
    store.set("temp", "world", 5000ms);
    EXPECT_EQ(store.pttl("temp"), 5000);
    EXPECT_EQ(store.ttl("temp"), 5);

    // Advance mock time by 2000ms
    advance_time(2000ms);
    EXPECT_EQ(store.pttl("temp"), 3000);
    EXPECT_EQ(store.ttl("temp"), 3);
}

TEST_F(StoreTest, PassiveExpiration) {
    Store store = create_mocked_store();

    store.set("session", "xyz", 1000ms);
    EXPECT_TRUE(store.exists("session"));

    // Advance past TTL
    advance_time(1001ms);

    // Key should passively expire on access
    EXPECT_FALSE(store.get("session").has_value());
    EXPECT_FALSE(store.exists("session"));
    EXPECT_EQ(store.ttl("session"), -2);
}

TEST_F(StoreTest, ExpireAndPexpire) {
    Store store = create_mocked_store();

    store.set("item", "data");
    EXPECT_EQ(store.ttl("item"), -1);

    // Set TTL in seconds
    EXPECT_TRUE(store.expire("item", 10s));
    EXPECT_EQ(store.ttl("item"), 10);

    // Overwrite TTL in milliseconds
    EXPECT_TRUE(store.pexpire("item", 2500ms));
    EXPECT_EQ(store.pttl("item"), 2500);

    // Expiring non-existent key returns false
    EXPECT_FALSE(store.expire("unknown", 5s));
}

TEST_F(StoreTest, PersistClearsTTL) {
    Store store = create_mocked_store();

    store.set("token", "abc", 5000ms);
    EXPECT_EQ(store.pttl("token"), 5000);
    EXPECT_EQ(store.expires_size(), 1u);

    EXPECT_TRUE(store.persist("token"));
    EXPECT_EQ(store.ttl("token"), -1);
    EXPECT_EQ(store.expires_size(), 0u);

    // Persist again returns false because no TTL exists
    EXPECT_FALSE(store.persist("token"));

    // Advance time past original TTL; key should still exist
    advance_time(10'000ms);
    EXPECT_EQ(store.get("token"), "abc");
}

TEST_F(StoreTest, OverwriteClearsPriorTTL) {
    Store store = create_mocked_store();

    store.set("k", "v1", 2000ms);
    EXPECT_EQ(store.expires_size(), 1u);

    // Standard SET without TTL clears previous TTL
    store.set("k", "v2");
    EXPECT_EQ(store.expires_size(), 0u);
    EXPECT_EQ(store.ttl("k"), -1);

    advance_time(3000ms);
    EXPECT_EQ(store.get("k"), "v2");
}

TEST_F(StoreTest, ZeroOrNegativeTTLDeletesKey) {
    Store store = create_mocked_store();

    store.set("k1", "v1");
    // Expire with 0s immediately removes key
    EXPECT_TRUE(store.expire("k1", 0s));
    EXPECT_FALSE(store.exists("k1"));

    // SET with negative TTL immediately removes key
    store.set("k2", "v2", -100ms);
    EXPECT_FALSE(store.exists("k2"));
}

TEST_F(StoreTest, ActiveExpireCycle) {
    Store store = create_mocked_store();

    // Create 30 keys with TTL
    for (int i = 0; i < 30; ++i) {
        store.set("exp_" + std::to_string(i), "val", 1000ms);
    }
    // Create 20 keys without TTL
    for (int i = 0; i < 20; ++i) {
        store.set("perm_" + std::to_string(i), "val");
    }

    EXPECT_EQ(store.expires_size(), 30u);
    EXPECT_EQ(store.dbsize(), 50u);

    // Before expiration, active cycle should purge 0 keys
    EXPECT_EQ(store.active_expire_cycle(20), 0u);

    // Advance time past TTL
    advance_time(1500ms);

    // Run active expire cycle: should purge the 30 expired keys
    size_t purged = store.active_expire_cycle(20);
    EXPECT_EQ(purged, 30u);
    EXPECT_EQ(store.expires_size(), 0u);
    EXPECT_EQ(store.dbsize(), 20u);

    // Verify all permanent keys are untouched
    for (int i = 0; i < 20; ++i) {
        EXPECT_TRUE(store.exists("perm_" + std::to_string(i)));
    }
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

    // Match all
    auto all = store.keys("*");
    EXPECT_EQ(all.size(), 5u);

    // Prefix match
    auto users = store.keys("user:*");
    EXPECT_EQ(users.size(), 3u);

    // Single character match '?'
    auto single_digit_users = store.keys("user:?");
    EXPECT_EQ(single_digit_users.size(), 2u);

    // Expired keys pruned during keys() enumeration
    advance_time(600ms);
    auto after_expiry = store.keys("*");
    EXPECT_EQ(after_expiry.size(), 4u);
    auto found_temp = std::find(after_expiry.begin(), after_expiry.end(), "temp");
    EXPECT_EQ(found_temp, after_expiry.end());
}
