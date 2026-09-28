#include "miniredis/command.h"
#include "miniredis/store.h"
#include "miniredis/telemetry.h"

#include <gtest/gtest.h>
#include <chrono>
#include <string>
#include <vector>

using namespace miniredis;
using namespace std::chrono_literals;

class ObservabilityTest : public ::testing::Test {
protected:
    Store::TimePoint mock_time = Store::TimePoint(1'000'000s);

    Store create_mocked_store() {
        return Store([this]() { return mock_time; });
    }

    void advance_time(std::chrono::milliseconds ms) {
        mock_time += ms;
    }
};

// -------------------------------------------------------------
// 1. Keyspace Hits, Misses, and Hit Ratio Correctness
// -------------------------------------------------------------
TEST_F(ObservabilityTest, KeyspaceHitsAndMisses) {
    Store store = create_mocked_store();

    EXPECT_EQ(store.metrics().keyspace_hits(), 0u);
    EXPECT_EQ(store.metrics().keyspace_misses(), 0u);
    EXPECT_DOUBLE_EQ(store.metrics().hit_ratio(), 0.0);

    store.set("user:100", "Alice");

    // 1. Successful GET -> Hit
    EXPECT_TRUE(store.get("user:100").has_value());
    EXPECT_EQ(store.metrics().keyspace_hits(), 1u);
    EXPECT_EQ(store.metrics().keyspace_misses(), 0u);
    EXPECT_DOUBLE_EQ(store.metrics().hit_ratio(), 1.0);

    // 2. Successful EXISTS -> Hit
    EXPECT_TRUE(store.exists("user:100"));
    EXPECT_EQ(store.metrics().keyspace_hits(), 2u);

    // 3. Failed GET -> Miss
    EXPECT_FALSE(store.get("missing_key").has_value());
    EXPECT_EQ(store.metrics().keyspace_hits(), 2u);
    EXPECT_EQ(store.metrics().keyspace_misses(), 1u);
    // 2 / (2 + 1) = 2/3 = 0.6666...
    EXPECT_NEAR(store.metrics().hit_ratio(), 0.6666, 0.001);

    // 4. Failed EXISTS -> Miss
    EXPECT_FALSE(store.exists("another_missing"));
    EXPECT_EQ(store.metrics().keyspace_hits(), 2u);
    EXPECT_EQ(store.metrics().keyspace_misses(), 2u);
    // 2 / (2 + 2) = 0.50
    EXPECT_DOUBLE_EQ(store.metrics().hit_ratio(), 0.50);
}

// -------------------------------------------------------------
// 2. Expired Keys Counter & Telemetry Events
// -------------------------------------------------------------
TEST_F(ObservabilityTest, ExpiredKeysCounterAndEvents) {
    Store store = create_mocked_store();

    EXPECT_EQ(store.metrics().expired_keys(), 0u);

    // Passive expiry check
    store.set("session_token", "active", 500ms);
    advance_time(501ms);

    // Trigger lazy expiry via GET -> misses increases, expired_keys increases
    EXPECT_FALSE(store.get("session_token").has_value());
    EXPECT_EQ(store.metrics().expired_keys(), 1u);
    EXPECT_EQ(store.metrics().keyspace_misses(), 1u);

    // Active expire cycle
    for (int i = 0; i < 20; ++i) {
        store.set("temp_" + std::to_string(i), "v", 200ms);
    }
    advance_time(300ms);

    auto stats = store.active_expire_cycle(20);
    EXPECT_EQ(stats.purged_keys, 20u);
    EXPECT_EQ(store.metrics().expired_keys(), 21u);

    // Verify telemetry ring buffer captured events
    auto recent = store.telemetry_buffer().get_recent(50);
    EXPECT_GE(recent.size(), 21u);
    EXPECT_EQ(recent[0].type, TelemetryEventType::EXPIRATION);
}

// -------------------------------------------------------------
// 3. Evicted Keys Counter & Memory Accounting
// -------------------------------------------------------------
TEST_F(ObservabilityTest, EvictedKeysCounter) {
    Store store = create_mocked_store();
    store.set_maxmemory_policy("allkeys-lru");

    store.set("k1", "val1");
    store.set("k2", "val2");
    size_t two_keys_mem = store.used_memory();
    store.set_maxmemory(two_keys_mem);

    EXPECT_EQ(store.eviction_buffer().total_evictions(), 0u);

    // Insert 3 more keys, forcing 3 evictions
    store.set("k3", "val3");
    store.set("k4", "val4");
    store.set("k5", "val5");

    EXPECT_EQ(store.eviction_buffer().total_evictions(), 3u);

    auto recent = store.eviction_buffer().get_recent(5);
    ASSERT_EQ(recent.size(), 3u);
    EXPECT_EQ(recent[0].policy, "allkeys-lru");
    EXPECT_GT(recent[0].freed_bytes, 0u);
}

// -------------------------------------------------------------
// 4. Total Commands Processed & INFO Output Fields
// -------------------------------------------------------------
TEST_F(ObservabilityTest, InfoCommandContainsAllRequiredFields) {
    Store store = create_mocked_store();

    // Execute known series of commands
    CommandDispatcher::execute({"SET", "name", "miniredis"}, store, 2);
    CommandDispatcher::execute({"GET", "name"}, store, 2);
    CommandDispatcher::execute({"GET", "missing"}, store, 2);
    CommandDispatcher::execute({"PING"}, store, 2);

    auto info_res = CommandDispatcher::execute({"INFO"}, store, 2);
    ASSERT_FALSE(info_res.response.empty());

    const std::string& info = info_res.response;

    // Verify all requested metric fields exist in INFO output
    EXPECT_NE(info.find("used_memory:"), std::string::npos);
    EXPECT_NE(info.find("total_keys:1"), std::string::npos);
    EXPECT_NE(info.find("keyspace_hits:1"), std::string::npos);
    EXPECT_NE(info.find("keyspace_misses:1"), std::string::npos);
    EXPECT_NE(info.find("hit_ratio:0.50"), std::string::npos);
    EXPECT_NE(info.find("evicted_keys:0"), std::string::npos);
    EXPECT_NE(info.find("expired_keys:0"), std::string::npos);
    EXPECT_NE(info.find("connected_clients:2"), std::string::npos);
    EXPECT_NE(info.find("total_commands_processed:5"), std::string::npos); // 4 + INFO
    EXPECT_NE(info.find("ops_per_sec:"), std::string::npos);
    EXPECT_NE(info.find("uptime_in_seconds:"), std::string::npos);
}

// -------------------------------------------------------------
// 5. EVENTS Command Telemetry Stream
// -------------------------------------------------------------
TEST_F(ObservabilityTest, EventsCommandExposesTelemetry) {
    Store store = create_mocked_store();

    // Trigger an eviction
    store.set_maxmemory_policy("allkeys-lru");
    store.set("a", "1");
    size_t one_key_mem = store.used_memory();
    store.set_maxmemory(one_key_mem);

    store.set("b", "2"); // evicts 'a'

    // Query EVENTS via dispatcher
    auto ev_res = CommandDispatcher::execute({"EVENTS", "LIMIT", "10"}, store);
    ASSERT_FALSE(ev_res.response.empty());
    // Should be a RESP array
    EXPECT_EQ(ev_res.response.front(), '*');
    EXPECT_NE(ev_res.response.find("EVICTION"), std::string::npos);
    EXPECT_NE(ev_res.response.find("a"), std::string::npos);
}
