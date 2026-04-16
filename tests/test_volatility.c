/**
 * test_volatility.c — Volatility Engine tests
 */

#include "test_harness.h"
#include "volatility.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

TEST(engine_open_close)
{
    const char *dir = "/tmp/sdb_test_engine_oc";
    cleanup_dir(dir);

    sdb_engine_t *engine = sdb_engine_open(dir);
    ASSERT_NE(engine, NULL);
    ASSERT_EQ(sdb_engine_count(engine), 0u);

    sdb_engine_close(engine);
    cleanup_dir(dir);
}

TEST(engine_store_locate_remove)
{
    const char *dir = "/tmp/sdb_test_engine_slr";
    cleanup_dir(dir);

    sdb_engine_t *engine = sdb_engine_open(dir);
    ASSERT_NE(engine, NULL);

    /* Create a record */
    sdb_record_t record;
    memset(&record, 0, sizeof(record));
    sdb_uuid_generate(&record.uuid);
    const char *data = "test payload";
    record.payload     = (uint8_t *)data;
    record.payload_len = strlen(data);
    record.state       = SDB_STATE_POTENTIAL;
    record.created_at  = (int64_t)time(NULL);

    ASSERT_EQ(sdb_engine_store(engine, &record), 0);
    ASSERT_EQ(sdb_engine_count(engine), 1u);

    /* Locate it */
    const uint8_t *ptr = NULL;
    size_t size = 0;
    ASSERT_EQ(sdb_engine_locate(engine, &record.uuid, &ptr, &size), 0);
    ASSERT_NE(ptr, NULL);
    ASSERT_TRUE(size > 0);

    /* Remove from index */
    ASSERT_EQ(sdb_engine_remove(engine, &record.uuid), 0);
    ASSERT_EQ(sdb_engine_count(engine), 0u);

    /* Locate should fail now */
    ASSERT_EQ(sdb_engine_locate(engine, &record.uuid, &ptr, &size), -1);

    record.payload = NULL; /* Don't free string literal */
    sdb_engine_close(engine);
    cleanup_dir(dir);
}

TEST(engine_index_persistence)
{
    const char *dir = "/tmp/sdb_test_engine_persist";
    cleanup_dir(dir);

    sdb_uuid_t stored_uuid;

    /* Store a record */
    {
        sdb_engine_t *engine = sdb_engine_open(dir);
        ASSERT_NE(engine, NULL);

        sdb_record_t record;
        memset(&record, 0, sizeof(record));
        sdb_uuid_generate(&record.uuid);
        memcpy(&stored_uuid, &record.uuid, sizeof(sdb_uuid_t));

        const char *data = "persistent data";
        record.payload     = (uint8_t *)data;
        record.payload_len = strlen(data);
        record.state       = SDB_STATE_POTENTIAL;
        record.created_at  = 1700000000;

        ASSERT_EQ(sdb_engine_store(engine, &record), 0);
        record.payload = NULL;
        sdb_engine_close(engine);
    }

    /* Reopen and verify */
    {
        sdb_engine_t *engine = sdb_engine_open(dir);
        ASSERT_NE(engine, NULL);
        ASSERT_EQ(sdb_engine_count(engine), 1u);

        const uint8_t *ptr = NULL;
        size_t size = 0;
        ASSERT_EQ(sdb_engine_locate(engine, &stored_uuid, &ptr, &size), 0);
        ASSERT_NE(ptr, NULL);
        ASSERT_TRUE(size > 0);

        sdb_engine_close(engine);
    }

    cleanup_dir(dir);
}

TEST(engine_secure_erase)
{
    const char *dir = "/tmp/sdb_test_engine_erase";
    cleanup_dir(dir);

    sdb_engine_t *engine = sdb_engine_open(dir);
    ASSERT_NE(engine, NULL);

    sdb_record_t record;
    memset(&record, 0, sizeof(record));
    sdb_uuid_generate(&record.uuid);
    const char *data = "SENSITIVE DATA";
    record.payload     = (uint8_t *)data;
    record.payload_len = strlen(data);
    record.state       = SDB_STATE_POTENTIAL;
    record.created_at  = (int64_t)time(NULL);

    ASSERT_EQ(sdb_engine_store(engine, &record), 0);
    ASSERT_EQ(sdb_engine_count(engine), 1u);

    /* Erase it */
    ASSERT_EQ(sdb_engine_erase(engine, &record.uuid), 0);
    ASSERT_EQ(sdb_engine_count(engine), 0u);

    /* Locate should fail now */
    const uint8_t *ptr = NULL;
    size_t size = 0;
    ASSERT_EQ(sdb_engine_locate(engine, &record.uuid, &ptr, &size), -1);

    record.payload = NULL;
    sdb_engine_close(engine);
    cleanup_dir(dir);
}
