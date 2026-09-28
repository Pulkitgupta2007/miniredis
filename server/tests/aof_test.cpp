#include "miniredis/aof.h"
#include "miniredis/store.h"

#include <gtest/gtest.h>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace miniredis;
using namespace std::chrono_literals;

class AofTest : public ::testing::Test {
protected:
    std::string test_aof_path = "/tmp/test_miniredis_aof.aof";
    int64_t simulated_epoch_ms = 1'700'000'000'000LL;

    void SetUp() override {
        std::remove(test_aof_path.c_str());
        std::remove((test_aof_path + ".rewrite.tmp").c_str());
    }

    void TearDown() override {
        std::remove(test_aof_path.c_str());
        std::remove((test_aof_path + ".rewrite.tmp").c_str());
    }

    AofManager create_aof_manager(FsyncPolicy policy = FsyncPolicy::ALWAYS) {
        return AofManager(test_aof_path, policy, [this]() { return simulated_epoch_ms; });
    }

    Store create_store() {
        return Store([this]() {
            return Store::TimePoint(std::chrono::milliseconds(simulated_epoch_ms));
        });
    }
};

// -------------------------------------------------------------
// 1. Basic Write, Restart, and Verify State
// -------------------------------------------------------------
TEST_F(AofTest, WriteRestartAndVerifyState) {
    // 1. Run commands on primary store and append to AOF
    Store store1 = create_store();
    AofManager aof1 = create_aof_manager(FsyncPolicy::ALWAYS);
    ASSERT_TRUE(aof1.open());

    store1.set("user:1", "alice");
    aof1.append_command({"SET", "user:1", "alice"});

    store1.set("user:2", "bob");
    aof1.append_command({"SET", "user:2", "bob"});

    store1.del("user:1");
    aof1.append_command({"DEL", "user:1"});

    aof1.close();

    // 2. Restart: simulate brand new server process with fresh empty Store
    Store store2 = create_store();
    AofManager aof2 = create_aof_manager(FsyncPolicy::ALWAYS);

    auto replay_res = aof2.replay(store2);
    EXPECT_TRUE(replay_res.success);
    EXPECT_EQ(replay_res.commands_replayed, 3u);
    EXPECT_FALSE(replay_res.truncated_recovered);

    // Verify replayed state matches primary store
    EXPECT_FALSE(store2.exists("user:1"));
    auto val2 = store2.get("user:2");
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(*val2, "bob");
    EXPECT_EQ(store2.dbsize(), 1u);
}

// -------------------------------------------------------------
// 2. Crash Recovery: Truncated Last Record
// -------------------------------------------------------------
TEST_F(AofTest, CrashRecoveryTruncatedLastRecord) {
    Store store1 = create_store();
    AofManager aof1 = create_aof_manager(FsyncPolicy::ALWAYS);
    ASSERT_TRUE(aof1.open());

    // Valid command 1
    store1.set("k1", "v1");
    aof1.append_command({"SET", "k1", "v1"});

    // Valid command 2
    store1.set("k2", "v2");
    aof1.append_command({"SET", "k2", "v2"});

    aof1.close();

    // Simulate crash mid-write: corrupt the tail by appending partial bytes
    FILE* fp = std::fopen(test_aof_path.c_str(), "ab");
    ASSERT_NE(fp, nullptr);
    const char* partial_frame = "*3\r\n$3\r\nSET\r\n$3\r\ncorrupted\r\n$10\r\npart";
    std::fwrite(partial_frame, 1, std::strlen(partial_frame), fp);
    std::fclose(fp);

    // Replay on restart
    Store store2 = create_store();
    AofManager aof2 = create_aof_manager(FsyncPolicy::ALWAYS);

    auto replay_res = aof2.replay(store2);
    EXPECT_TRUE(replay_res.success);
    // Should successfully replay the 2 valid commands and recover from truncated tail
    EXPECT_EQ(replay_res.commands_replayed, 2u);
    EXPECT_TRUE(replay_res.truncated_recovered);

    EXPECT_EQ(store2.get("k1"), "v1");
    EXPECT_EQ(store2.get("k2"), "v2");
    EXPECT_FALSE(store2.exists("corrupted"));
}

// -------------------------------------------------------------
// 3. Expired Keys Are Not Resurrected on Replay
// -------------------------------------------------------------
TEST_F(AofTest, ExpiredKeysNotResurrectedOnReplay) {
    Store store1 = create_store();
    AofManager aof1 = create_aof_manager(FsyncPolicy::ALWAYS);
    ASSERT_TRUE(aof1.open());

    // Key with 5000ms TTL: absolute expiration is simulated_epoch_ms + 5000
    int64_t expire_at = simulated_epoch_ms + 5000;
    store1.set("temp_key", "temporary_val");
    store1.pexpireat("temp_key", expire_at, simulated_epoch_ms);

    aof1.append_command({"SET", "temp_key", "temporary_val"});
    aof1.append_command({"PEXPIREAT", "temp_key", std::to_string(expire_at)});

    // Permanent key
    store1.set("perm_key", "permanent_val");
    aof1.append_command({"SET", "perm_key", "permanent_val"});

    aof1.close();

    // Fast-forward simulated time by 10,000ms (key has now expired)
    simulated_epoch_ms += 10'000;

    // Replay on restart at new time
    Store store2 = create_store();
    AofManager aof2 = create_aof_manager(FsyncPolicy::ALWAYS);

    auto replay_res = aof2.replay(store2);
    EXPECT_TRUE(replay_res.success);

    // Expired key MUST NOT be resurrected!
    EXPECT_FALSE(store2.exists("temp_key"));
    EXPECT_FALSE(store2.get("temp_key").has_value());

    // Permanent key remains
    EXPECT_TRUE(store2.exists("perm_key"));
    EXPECT_EQ(store2.get("perm_key"), "permanent_val");
}

// -------------------------------------------------------------
// 4. AOF Rewrite / Compaction Produces Minimal Equivalent State
// -------------------------------------------------------------
TEST_F(AofTest, RewriteCompactionProducesEquivalentState) {
    Store store1 = create_store();
    AofManager aof1 = create_aof_manager(FsyncPolicy::ALWAYS);
    ASSERT_TRUE(aof1.open());

    // Create a key that gets updated 50 times (bloats log)
    for (int i = 0; i < 50; ++i) {
        store1.set("counter", std::to_string(i));
        aof1.append_command({"SET", "counter", std::to_string(i)});
    }

    // Create 10 keys and delete 5 of them
    for (int i = 0; i < 10; ++i) {
        std::string k = "k_" + std::to_string(i);
        store1.set(k, "val");
        aof1.append_command({"SET", k, "val"});
    }
    for (int i = 0; i < 5; ++i) {
        std::string k = "k_" + std::to_string(i);
        store1.del(k);
        aof1.append_command({"DEL", k});
    }

    size_t size_before = aof1.file_size();
    EXPECT_GT(size_before, 1500u);

    // Perform AOF compaction / rewrite
    ASSERT_TRUE(aof1.rewrite(store1));

    size_t size_after = aof1.file_size();
    EXPECT_LT(size_after, size_before);

    aof1.close();

    // Replay compacted AOF into fresh store and verify exact equivalence
    Store store2 = create_store();
    AofManager aof2 = create_aof_manager(FsyncPolicy::ALWAYS);

    auto replay_res = aof2.replay(store2);
    EXPECT_TRUE(replay_res.success);

    // Verify compacted store matches original store
    EXPECT_EQ(store2.dbsize(), store1.dbsize());
    EXPECT_EQ(store2.get("counter"), "49");

    for (int i = 0; i < 5; ++i) {
        EXPECT_FALSE(store2.exists("k_" + std::to_string(i)));
    }
    for (int i = 5; i < 10; ++i) {
        EXPECT_TRUE(store2.exists("k_" + std::to_string(i)));
    }
}

// -------------------------------------------------------------
// 5. Fsync Policies Configuration
// -------------------------------------------------------------
TEST_F(AofTest, FsyncPoliciesConfiguration) {
    AofManager aof(test_aof_path, FsyncPolicy::NO);
    EXPECT_EQ(aof.get_fsync_policy_str(), "no");

    aof.set_fsync_policy(FsyncPolicy::EVERYSEC);
    EXPECT_EQ(aof.get_fsync_policy_str(), "everysec");

    aof.set_fsync_policy(FsyncPolicy::ALWAYS);
    EXPECT_EQ(aof.get_fsync_policy_str(), "always");
}
