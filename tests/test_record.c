/**
 * test_record.c — Record tests
 */

#include "test_harness.h"
#include "record.h"

#include <string.h>

TEST(uuid_generate_unique)
{
    sdb_uuid_t a, b;
    ASSERT_EQ(sdb_uuid_generate(&a), 0);
    ASSERT_EQ(sdb_uuid_generate(&b), 0);
    ASSERT_NE(sdb_uuid_compare(&a, &b), 0);
}

TEST(uuid_to_string_roundtrip)
{
    sdb_uuid_t original, parsed;
    ASSERT_EQ(sdb_uuid_generate(&original), 0);

    char str[33];
    ASSERT_EQ(sdb_uuid_to_string(&original, str, sizeof(str)), 0);
    ASSERT_EQ(strlen(str), 32u);

    ASSERT_EQ(sdb_uuid_from_string(str, &parsed), 0);
    ASSERT_EQ(sdb_uuid_compare(&original, &parsed), 0);
}

TEST(uuid_from_string_invalid)
{
    sdb_uuid_t uuid;
    ASSERT_EQ(sdb_uuid_from_string("short", &uuid), -1);
    ASSERT_EQ(sdb_uuid_from_string(NULL, &uuid), -1);
}

TEST(record_serialize_deserialize)
{
    sdb_record_t record;
    memset(&record, 0, sizeof(record));
    sdb_uuid_generate(&record.uuid);
    record.state      = SDB_STATE_POTENTIAL;
    record.created_at = 1700000000;

    const char *data = "Hello, Schrödinger!";
    record.payload_len = strlen(data);
    record.payload     = (uint8_t *)data;

    size_t ser_size = sdb_record_serialized_size(&record);
    ASSERT_EQ(ser_size, SDB_RECORD_HEADER_SIZE + record.payload_len);

    uint8_t *buf = malloc(ser_size);
    ASSERT_NE(buf, NULL);

    ssize_t written = sdb_record_serialize(&record, buf, ser_size);
    ASSERT_EQ((size_t)written, ser_size);

    sdb_record_t out;
    ASSERT_EQ(sdb_record_deserialize(buf, ser_size, &out), 0);
    ASSERT_EQ(sdb_uuid_compare(&record.uuid, &out.uuid), 0);
    ASSERT_EQ(out.state, SDB_STATE_POTENTIAL);
    ASSERT_EQ(out.created_at, 1700000000);
    ASSERT_EQ(out.payload_len, record.payload_len);
    ASSERT_EQ(memcmp(out.payload, data, record.payload_len), 0);

    sdb_record_free(&out);
    free(buf);

    /* Don't free record.payload — it's a string literal pointer. */
    record.payload = NULL;
}

TEST(record_deserialize_truncated)
{
    uint8_t buf[10];
    memset(buf, 0, sizeof(buf));

    sdb_record_t out;
    ASSERT_EQ(sdb_record_deserialize(buf, sizeof(buf), &out), -1);
}
