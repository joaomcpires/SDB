/**
 * test_commands.c — Core command tests (PRAY, OBSERVE, MUTATE, FORGET)
 */

#include "test_harness.h"
#include "sdb.h"
#include "logging.h"

#include <stdio.h>
#include <string.h>

TEST(pray_always_succeeds)
{
    const char *dir = "/tmp/sdb_test_cmd_pray";
    cleanup_dir(dir);

    sdb_log_set_level(SDB_LOG_CRITICAL); /* Suppress info logs in tests */

    sdb_entropy_source_t *ent = sdb_entropy_deterministic_create(42);
    sdb_t *db = sdb_open(dir, ent);
    ASSERT_NE(db, NULL);

    const char *data = "prayer data";
    sdb_uuid_t uuid;
    ASSERT_EQ(sdb_pray(db, (const uint8_t *)data, strlen(data), &uuid), 0);

    /* UUID should be nonzero */
    int all_zero = 1;
    for (int i = 0; i < 16; i++) {
        if (uuid.bytes[i] != 0) { all_zero = 0; break; }
    }
    ASSERT_EQ(all_zero, 0);

    sdb_close(db);
    cleanup_dir(dir);
}

TEST(observe_stable_returns_data)
{
    const char *dir = "/tmp/sdb_test_cmd_obs_stable";
    cleanup_dir(dir);

    sdb_log_set_level(SDB_LOG_CRITICAL);

    /*
     * We need a seed that produces 0 (Stable) on the first collapse_bit call.
     * xorshift64 with seed=42:
     *   state ^= state << 13 => ...
     * Let's just try seed 42 and check.
     * We'll insert and try to observe; if collapsed, note and skip.
     */
    sdb_entropy_source_t *ent = sdb_entropy_deterministic_create(2);
    /* Seed 2: xorshift64(2) -> first bit check */
    sdb_t *db = sdb_open(dir, ent);
    ASSERT_NE(db, NULL);

    /* Insert multiple records and observe until we get a stable one */
    const char *data = "observable data";
    int got_stable = 0;

    for (int i = 0; i < 20 && !got_stable; i++) {
        sdb_uuid_t uuid;
        ASSERT_EQ(sdb_pray(db, (const uint8_t *)data, strlen(data), &uuid), 0);

        uint8_t buf[256];
        size_t len = 0;
        sdb_observe_result_t res = sdb_observe(db, &uuid, buf, sizeof(buf), &len);

        if (res == SDB_OBSERVE_OK) {
            ASSERT_EQ(len, strlen(data));
            ASSERT_EQ(memcmp(buf, data, len), 0);
            got_stable = 1;
        }
        /* If collapsed, keep trying with new records */
    }

    ASSERT_TRUE(got_stable);

    sdb_close(db);
    cleanup_dir(dir);
}

TEST(observe_not_found)
{
    const char *dir = "/tmp/sdb_test_cmd_obs_nf";
    cleanup_dir(dir);

    sdb_log_set_level(SDB_LOG_CRITICAL);

    sdb_entropy_source_t *ent = sdb_entropy_deterministic_create(42);
    sdb_t *db = sdb_open(dir, ent);
    ASSERT_NE(db, NULL);

    sdb_uuid_t fake;
    memset(&fake, 0xFF, sizeof(fake));

    sdb_observe_result_t res = sdb_observe(db, &fake, NULL, 0, NULL);
    ASSERT_EQ(res, SDB_OBSERVE_NOT_FOUND);

    sdb_close(db);
    cleanup_dir(dir);
}

TEST(forget_always_succeeds)
{
    const char *dir = "/tmp/sdb_test_cmd_forget";
    cleanup_dir(dir);

    sdb_log_set_level(SDB_LOG_CRITICAL);

    sdb_entropy_source_t *ent = sdb_entropy_deterministic_create(42);
    sdb_t *db = sdb_open(dir, ent);
    ASSERT_NE(db, NULL);

    const char *data = "forgettable data";
    sdb_uuid_t uuid;
    ASSERT_EQ(sdb_pray(db, (const uint8_t *)data, strlen(data), &uuid), 0);
    ASSERT_EQ(sdb_forget(db, &uuid), 0);

    /* Observe should now return not found */
    sdb_observe_result_t res = sdb_observe(db, &uuid, NULL, 0, NULL);
    ASSERT_EQ(res, SDB_OBSERVE_NOT_FOUND);

    sdb_close(db);
    cleanup_dir(dir);
}

TEST(observe_statistical_50_percent)
{
    const char *dir = "/tmp/sdb_test_cmd_obs_stat";
    cleanup_dir(dir);

    sdb_log_set_level(SDB_LOG_CRITICAL);

    sdb_entropy_source_t *ent = sdb_entropy_deterministic_create(12345);
    sdb_t *db = sdb_open(dir, ent);
    ASSERT_NE(db, NULL);

    int n = 100;
    int stable = 0;
    int unstable = 0;

    for (int i = 0; i < n; i++) {
        const char *data = "test";
        sdb_uuid_t uuid;
        ASSERT_EQ(sdb_pray(db, (const uint8_t *)data, strlen(data), &uuid), 0);

        sdb_observe_result_t res = sdb_observe(db, &uuid, NULL, 0, NULL);
        if (res == SDB_OBSERVE_OK)
            stable++;
        else if (res == SDB_OBSERVE_COLLAPSED)
            unstable++;
    }

    /* With 100 trials, expect roughly 50/50. Allow ±20 margin. */
    ASSERT_GE(stable, 30);
    ASSERT_LE(stable, 70);
    ASSERT_GE(unstable, 30);
    ASSERT_LE(unstable, 70);

    sdb_close(db);
    cleanup_dir(dir);
}

struct test_track_ctx {
    int count;
};

static void test_track_cb(const char *uuid_str, void *user_data)
{
    (void)uuid_str;
    struct test_track_ctx *ctx = user_data;
    ctx->count++;
}

TEST(track_safely_lists_records)
{
    const char *dir = "/tmp/sdb_test_cmd_track";
    cleanup_dir(dir);

    sdb_log_set_level(SDB_LOG_CRITICAL);

    sdb_entropy_source_t *ent = sdb_entropy_deterministic_create(42);
    sdb_t *db = sdb_open(dir, ent);
    ASSERT_NE(db, NULL);

    int n = 10;
    for (int i = 0; i < n; i++) {
        const char *data = "test data";
        sdb_uuid_t uuid;
        ASSERT_EQ(sdb_pray(db, (const uint8_t *)data, strlen(data), &uuid), 0);
    }

    struct test_track_ctx tctx = {0};
    ASSERT_EQ(sdb_track(db, test_track_cb, &tctx), 0);
    
    ASSERT_EQ(tctx.count, n);

    /* Verify no records were collapsed during track */
    ASSERT_EQ(sdb_engine_count(db->engine), (size_t)n);

    size_t survived = 0, collapsed = 0;
    ASSERT_EQ(sdb_count(db, &survived, &collapsed), 0);
    ASSERT_EQ(survived + collapsed, (size_t)n);

    sdb_close(db);
    cleanup_dir(dir);
}
