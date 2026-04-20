/**
 * sdb.h — Schrödinger's Database Public API
 *
 * "In SDB, your data is safe as long as you don't care to see it."
 *
 * Top-level header that provides the database handle and the four
 * core commands: PRAY, OBSERVE, MUTATE, FORGET, plus aggregates.
 */

#ifndef SDB_H
#define SDB_H

#include "entropy.h"
#include "errors.h"
#include "record.h"
#include "volatility.h"

#include <stddef.h>
#include <stdint.h>

/**
 * The SDB database handle.
 */
typedef struct {
    sdb_engine_t         *engine;
    sdb_entropy_source_t *entropy;
} sdb_t;

/**
 * Open an SDB instance.
 *
 * @param data_dir  Path to the storage directory.
 * @param entropy   Entropy source (ownership transferred to SDB).
 * @return Database handle, or NULL on failure.
 */
sdb_t *sdb_open(const char *data_dir, sdb_entropy_source_t *entropy);

/**
 * Close an SDB instance. Persists the index and frees all resources.
 */
void sdb_close(sdb_t *db);

/* ── Core Commands ─────────────────────────────────────────────────── */

/**
 * PRAY — Commit data to the void.
 *
 * Always succeeds. Future availability is not guaranteed.
 *
 * @param db        Database handle.
 * @param data      Payload bytes.
 * @param data_len  Payload length.
 * @param out_uuid  Receives the assigned UUID.
 * @return 0 on success, -1 on failure.
 */
int sdb_pray(sdb_t *db, const uint8_t *data, size_t data_len,
             sdb_uuid_t *out_uuid);

/**
 * OBSERVE — Attempt to retrieve data. 50% risk of permanent erasure.
 *
 * @param db       Database handle.
 * @param uuid     Record UUID.
 * @param out_buf  Buffer to receive payload (caller-allocated).
 * @param buf_len  Size of out_buf.
 * @param out_len  Receives actual payload length (may be NULL).
 * @return SDB_OBSERVE_OK, SDB_OBSERVE_COLLAPSED, or SDB_OBSERVE_NOT_FOUND.
 */
sdb_observe_result_t sdb_observe(sdb_t *db, const sdb_uuid_t *uuid,
                                 uint8_t *out_buf, size_t buf_len,
                                 size_t *out_len);

/**
 * MUTATE — Attempt to modify data. ~25% success rate (50% × 50%).
 *
 * @param db            Database handle.
 * @param uuid          Record UUID.
 * @param new_data      New payload bytes.
 * @param new_data_len  New payload length.
 * @param out_new_uuid  Receives the UUID of the new record (if successful).
 * @return SDB_MUTATE_OK, SDB_MUTATE_ORIGINAL_COLLAPSED,
 *         SDB_MUTATE_WRITE_ABORTED, or SDB_MUTATE_NOT_FOUND.
 */
sdb_mutate_result_t sdb_mutate(sdb_t *db, const sdb_uuid_t *uuid,
                               const uint8_t *new_data, size_t new_data_len,
                               sdb_uuid_t *out_new_uuid);

/**
 * FORGET — Delete with absolute certainty. Always succeeds.
 *
 * @param db    Database handle.
 * @param uuid  Record UUID.
 * @return 0 on success, -1 if not found.
 */
int sdb_forget(sdb_t *db, const sdb_uuid_t *uuid);

/* ── Aggregate Operations ──────────────────────────────────────────── */

/**
 * Numeric extractor function type.
 * Given a record payload, extract a double value.
 */
typedef double (*sdb_extractor_fn)(const uint8_t *payload, size_t len);

/**
 * COUNT — Destructive count. Each record has a 50% collapse chance.
 *
 * @param db             Database handle.
 * @param out_surviving  Receives surviving record count.
 * @param out_collapsed  Receives collapsed record count.
 * @return 0 on success, -1 on error.
 */
int sdb_count(sdb_t *db, size_t *out_surviving, size_t *out_collapsed);

/**
 * SUM — Destructive sum. Each record observed independently.
 *
 * @param db             Database handle.
 * @param extractor      Function to extract numeric value from payload.
 * @param out_sum        Receives computed sum of survivors.
 * @param out_collapsed  Receives collapsed record count.
 * @return 0 on success, -1 on error.
 */
int sdb_sum(sdb_t *db, sdb_extractor_fn extractor,
            double *out_sum, size_t *out_collapsed);

/**
 * AVG — Destructive average. Built on SUM / COUNT of survivors.
 *
 * @param db             Database handle.
 * @param extractor      Function to extract numeric value from payload.
 * @param out_avg        Receives computed average of survivors.
 * @param out_collapsed  Receives collapsed record count.
 * @return 0 on success, -1 on error.
 */
int sdb_avg(sdb_t *db, sdb_extractor_fn extractor,
            double *out_avg, size_t *out_collapsed);

/** Callback type for sdb_track. */
typedef void (*sdb_track_cb)(const char *uuid_str, void *user_data);

/**
 * TRACK — Safely list QDI (UUIDs) without collapsing records.
 * 100% success rate. Bypasses the observer effect.
 *
 * @param db         Database handle.
 * @param callback   Function called for each UUID string.
 * @param user_data  Opaque pointer passed to callback.
 * @return 0 on success, -1 on error.
 */
int sdb_track(sdb_t *db, sdb_track_cb callback, void *user_data);

#endif /* SDB_H */
