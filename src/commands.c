/**
 * commands.c — SDB Core Commands
 *
 * PRAY, OBSERVE, MUTATE, FORGET — the four pillars of SDB.
 */

#include "sdb.h"
#include "logging.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── SDB lifecycle ─────────────────────────────────────────────────── */

sdb_t *sdb_open(const char *data_dir, sdb_entropy_source_t *entropy)
{
    if (!data_dir || !entropy) {
        errno = EINVAL;
        return NULL;
    }

    sdb_t *db = calloc(1, sizeof(sdb_t));
    if (!db)
        return NULL;

    db->engine = sdb_engine_open(data_dir);
    if (!db->engine) {
        free(db);
        return NULL;
    }

    db->entropy = entropy;

    sdb_log(SDB_LOG_INFO, "INIT: Database opened. Data directory: %s. "
            "Index contains %zu records.", data_dir,
            sdb_engine_count(db->engine));

    return db;
}

void sdb_close(sdb_t *db)
{
    if (!db)
        return;

    sdb_log(SDB_LOG_INFO, "SHUTDOWN: Persisting index. %zu records tracked.",
            sdb_engine_count(db->engine));

    sdb_engine_close(db->engine);
    sdb_entropy_destroy(db->entropy);
    free(db);
}

/* ── PRAY ──────────────────────────────────────────────────────────── */

int sdb_pray(sdb_t *db, const uint8_t *data, size_t data_len,
             sdb_uuid_t *out_uuid)
{
    if (!db || !data || !out_uuid) {
        errno = EINVAL;
        return -1;
    }

    sdb_record_t record;
    memset(&record, 0, sizeof(record));

    if (sdb_uuid_generate(&record.uuid) != 0)
        return -1;

    record.payload = malloc(data_len);
    if (!record.payload && data_len > 0)
        return -1;

    memcpy(record.payload, data, data_len);
    record.payload_len = data_len;
    record.state       = SDB_STATE_POTENTIAL;
    record.created_at  = (int64_t)time(NULL);

    int ret = sdb_engine_store(db->engine, &record);

    if (ret == 0) {
        memcpy(out_uuid, &record.uuid, sizeof(sdb_uuid_t));

        char uuid_str[33];
        sdb_uuid_to_string(&record.uuid, uuid_str, sizeof(uuid_str));
        sdb_log(SDB_LOG_INFO, "PRAY: Record %s stored. Payload: %zu bytes.",
                uuid_str, data_len);
    }

    sdb_record_free(&record);
    return ret;
}

/* ── OBSERVE ───────────────────────────────────────────────────────── */

sdb_observe_result_t sdb_observe(sdb_t *db, const sdb_uuid_t *uuid,
                                 uint8_t *out_buf, size_t buf_len,
                                 size_t *out_len)
{
    if (!db || !uuid)
        return SDB_OBSERVE_NOT_FOUND;

    char uuid_str[33];
    sdb_uuid_to_string(uuid, uuid_str, sizeof(uuid_str));

    /* Locate record in arena without reading content */
    const uint8_t *arena_ptr = NULL;
    size_t record_size = 0;
    if (sdb_engine_locate(db->engine, uuid, &arena_ptr, &record_size) != 0) {
        sdb_log(SDB_LOG_INFO, "OBSERVE: Record %s not found in index.",
                uuid_str);
        return SDB_OBSERVE_NOT_FOUND;
    }

    /* Pull collapse bit */
    int bit = db->entropy->collapse_bit(db->entropy);
    if (bit < 0) {
        sdb_log(SDB_LOG_CRITICAL, "OBSERVE: Entropy source failure for "
                "record %s.", uuid_str);
        return SDB_OBSERVE_NOT_FOUND; /* Fail safe: don't destroy */
    }

    if (bit == 1) {
        /* UNSTABLE — erase the record */
        sdb_engine_erase(db->engine, uuid);

        sdb_log(SDB_LOG_CRITICAL, "OBSERVE: Quantum Collapse: Record %s "
                "transitioned to state 1 (Unstable). Secure erase completed.",
                uuid_str);

        return SDB_OBSERVE_COLLAPSED;
    }

    /* STABLE — deserialize from arena */
    sdb_record_t record;
    if (sdb_record_deserialize(arena_ptr, record_size, &record) != 0)
        return SDB_OBSERVE_NOT_FOUND;

    /* Copy payload to output buffer */
    if (out_buf && buf_len > 0) {
        size_t copy_len = record.payload_len < buf_len
                              ? record.payload_len
                              : buf_len;
        memcpy(out_buf, record.payload, copy_len);
    }
    if (out_len)
        *out_len = record.payload_len;

    sdb_log(SDB_LOG_INFO, "OBSERVE: Record %s transitioned to state 0 "
            "(Stable). Payload: %zu bytes returned.", uuid_str,
            record.payload_len);

    sdb_record_free(&record);
    return SDB_OBSERVE_OK;
}

/* ── MUTATE ────────────────────────────────────────────────────────── */

sdb_mutate_result_t sdb_mutate(sdb_t *db, const sdb_uuid_t *uuid,
                               const uint8_t *new_data, size_t new_data_len,
                               sdb_uuid_t *out_new_uuid)
{
    if (!db || !uuid || !new_data)
        return SDB_MUTATE_NOT_FOUND;

    char uuid_str[33];
    sdb_uuid_to_string(uuid, uuid_str, sizeof(uuid_str));

    /* Step 1: Observe the original (50% chance of destruction) */
    sdb_observe_result_t obs = sdb_observe(db, uuid, NULL, 0, NULL);

    if (obs == SDB_OBSERVE_NOT_FOUND) {
        sdb_log(SDB_LOG_INFO, "MUTATE: Record %s not found.", uuid_str);
        return SDB_MUTATE_NOT_FOUND;
    }

    if (obs == SDB_OBSERVE_COLLAPSED) {
        sdb_log(SDB_LOG_CRITICAL, "MUTATE: Record %s collapsed during "
                "observation phase. Mutation aborted.", uuid_str);
        return SDB_MUTATE_ORIGINAL_COLLAPSED;
    }

    /* Step 2: Second entropy gate (another 50% chance) */
    int bit = db->entropy->collapse_bit(db->entropy);
    if (bit < 0 || bit == 1) {
        /* Write gate failed — erase original */
        sdb_engine_erase(db->engine, uuid);

        sdb_log(SDB_LOG_CRITICAL, "MUTATE: Write gate collapsed for record "
                "%s. Original erased, new data discarded.", uuid_str);
        return SDB_MUTATE_WRITE_ABORTED;
    }

    /* Step 3: Write new data */
    sdb_uuid_t new_uuid;
    if (sdb_pray(db, new_data, new_data_len, &new_uuid) != 0) {
        sdb_log(SDB_LOG_CRITICAL, "MUTATE: Write failed for record %s.",
                uuid_str);
        return SDB_MUTATE_WRITE_ABORTED;
    }

    /* Step 4: Forget the old record */
    sdb_forget(db, uuid);

    if (out_new_uuid)
        memcpy(out_new_uuid, &new_uuid, sizeof(sdb_uuid_t));

    char new_uuid_str[33];
    sdb_uuid_to_string(&new_uuid, new_uuid_str, sizeof(new_uuid_str));
    sdb_log(SDB_LOG_INFO, "MUTATE: Record %s replaced by %s. Mutation "
            "successful.", uuid_str, new_uuid_str);

    return SDB_MUTATE_OK;
}

/* ── FORGET ────────────────────────────────────────────────────────── */

int sdb_forget(sdb_t *db, const sdb_uuid_t *uuid)
{
    if (!db || !uuid) {
        errno = EINVAL;
        return -1;
    }

    char uuid_str[33];
    sdb_uuid_to_string(uuid, uuid_str, sizeof(uuid_str));

    if (sdb_engine_erase(db->engine, uuid) != 0) {
        sdb_log(SDB_LOG_INFO, "FORGET: Record %s not found.", uuid_str);
        return -1;
    }

    sdb_log(SDB_LOG_INFO, "FORGET: Record %s erased with certainty.",
            uuid_str);

    return 0;
}
