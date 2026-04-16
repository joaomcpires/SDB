/**
 * test_aggregate.c — Aggregate operation tests (COUNT, SUM, AVG)
 */

#include "test_harness.h"
#include "sdb.h"
#include "logging.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

TEST(count_destructive_approximately_50_percent)
{
    const char *dir = "/tmp/sdb_test_agg_count";
    cleanup_dir(dir);

    sdb_log_set_level(SDB_LOG_CRITICAL);

    sdb_entropy_source_t *ent = sdb_entropy_deterministic_create(9999);
    sdb_t *db = sdb_open(dir, ent);
    ASSERT_NE(db, NULL);

    int n = 100;
    for (int i = 0; i < n; i++) {
        const char *data = "countable";
        sdb_uuid_t uuid;
        ASSERT_EQ(sdb_pray(db, (const uint8_t *)data, strlen(data), &uuid), 0);
    }

    size_t survived = 0, collapsed = 0;
    ASSERT_EQ(sdb_count(db, &survived, &collapsed), 0);

    /* Total should equal n */
    ASSERT_EQ(survived + collapsed, (size_t)n);

    /* Expect ~50% survival, allow ±20 margin */
    ASSERT_GE(survived, 30u);
    ASSERT_LE(survived, 70u);

    sdb_close(db);
    cleanup_dir(dir);
}

/* Simple extractor: interpret payload as a double string */
static double extract_double(const uint8_t *payload, size_t len)
{
    char buf[64];
    size_t copy = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, payload, copy);
    buf[copy] = '\0';
    return atof(buf);
}

TEST(sum_destructive)
{
    const char *dir = "/tmp/sdb_test_agg_sum";
    cleanup_dir(dir);

    sdb_log_set_level(SDB_LOG_CRITICAL);

    sdb_entropy_source_t *ent = sdb_entropy_deterministic_create(7777);
    sdb_t *db = sdb_open(dir, ent);
    ASSERT_NE(db, NULL);

    /* Insert 20 records with value "10.0" */
    for (int i = 0; i < 20; i++) {
        const char *data = "10.0";
        sdb_uuid_t uuid;
        ASSERT_EQ(sdb_pray(db, (const uint8_t *)data, strlen(data), &uuid), 0);
    }

    double sum = 0.0;
    size_t collapsed = 0;
    ASSERT_EQ(sdb_sum(db, extract_double, &sum, &collapsed), 0);

    /* Sum should be 10.0 * number of survivors */
    size_t survived = 20 - collapsed;
    double expected = 10.0 * (double)survived;
    ASSERT_TRUE(fabs(sum - expected) < 0.01);

    sdb_close(db);
    cleanup_dir(dir);
}

TEST(avg_destructive)
{
    const char *dir = "/tmp/sdb_test_agg_avg";
    cleanup_dir(dir);

    sdb_log_set_level(SDB_LOG_CRITICAL);

    sdb_entropy_source_t *ent = sdb_entropy_deterministic_create(5555);
    sdb_t *db = sdb_open(dir, ent);
    ASSERT_NE(db, NULL);

    /* Insert 20 records with value "25.0" */
    for (int i = 0; i < 20; i++) {
        const char *data = "25.0";
        sdb_uuid_t uuid;
        ASSERT_EQ(sdb_pray(db, (const uint8_t *)data, strlen(data), &uuid), 0);
    }

    double avg = 0.0;
    size_t collapsed = 0;
    ASSERT_EQ(sdb_avg(db, extract_double, &avg, &collapsed), 0);

    /* All records have the same value, so avg should be ~25.0 */
    if (collapsed < 20)
        ASSERT_TRUE(fabs(avg - 25.0) < 0.01);

    sdb_close(db);
    cleanup_dir(dir);
}
