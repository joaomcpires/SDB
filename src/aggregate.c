/**
 * aggregate.c — SDB Aggregate Operations
 *
 * COUNT, SUM, AVG — each record is individually observed during
 * aggregation, giving every record a 50% chance of collapse.
 */

#include "sdb.h"
#include "logging.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Collect all UUIDs first (to avoid mutating during iteration) ─── */

typedef struct {
    sdb_uuid_t *uuids;
    size_t      count;
    size_t      capacity;
} uuid_list_t;

static int collect_uuid(const sdb_uuid_t *key, sdb_record_meta_t *value,
                        void *user_data)
{
    (void)value;
    uuid_list_t *list = (uuid_list_t *)user_data;

    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity * 2;
        sdb_uuid_t *new_buf = realloc(list->uuids,
                                      new_cap * sizeof(sdb_uuid_t));
        if (!new_buf)
            return -1;
        list->uuids   = new_buf;
        list->capacity = new_cap;
    }

    memcpy(&list->uuids[list->count], key, sizeof(sdb_uuid_t));
    list->count++;
    return 0;
}

static int collect_all_uuids(sdb_t *db, uuid_list_t *list)
{
    size_t n = sdb_engine_count(db->engine);
    list->capacity = n > 0 ? n : 16;
    list->count    = 0;
    list->uuids    = malloc(list->capacity * sizeof(sdb_uuid_t));
    if (!list->uuids)
        return -1;

    sdb_hashmap_t *index = sdb_engine_get_index(db->engine);
    return sdb_hashmap_iterate(index, collect_uuid, list);
}

/* ── COUNT ─────────────────────────────────────────────────────────── */

int sdb_count(sdb_t *db, size_t *out_surviving, size_t *out_collapsed)
{
    if (!db)
        return -1;

    uuid_list_t list;
    if (collect_all_uuids(db, &list) != 0)
        return -1;

    size_t survived = 0;
    size_t collapsed = 0;

    for (size_t i = 0; i < list.count; i++) {
        sdb_observe_result_t res = sdb_observe(db, &list.uuids[i],
                                               NULL, 0, NULL);
        if (res == SDB_OBSERVE_OK)
            survived++;
        else if (res == SDB_OBSERVE_COLLAPSED)
            collapsed++;
    }

    if (out_surviving)  *out_surviving  = survived;
    if (out_collapsed)  *out_collapsed  = collapsed;

    sdb_log(SDB_LOG_INFO, "COUNT: Aggregation complete. Survived: %zu, "
            "collapsed: %zu.", survived, collapsed);

    free(list.uuids);
    return 0;
}

/* ── SUM ───────────────────────────────────────────────────────────── */

int sdb_sum(sdb_t *db, sdb_extractor_fn extractor,
            double *out_sum, size_t *out_collapsed)
{
    if (!db || !extractor)
        return -1;

    uuid_list_t list;
    if (collect_all_uuids(db, &list) != 0)
        return -1;

    double sum = 0.0;
    size_t collapsed = 0;

    /* Allocate a reasonable read buffer */
    size_t buf_size = 64 * 1024;
    uint8_t *buf = malloc(buf_size);
    if (!buf) {
        free(list.uuids);
        return -1;
    }

    for (size_t i = 0; i < list.count; i++) {
        size_t payload_len = 0;
        sdb_observe_result_t res = sdb_observe(db, &list.uuids[i],
                                               buf, buf_size, &payload_len);
        if (res == SDB_OBSERVE_OK) {
            sum += extractor(buf, payload_len);
        } else if (res == SDB_OBSERVE_COLLAPSED) {
            collapsed++;
        }
    }

    if (out_sum)       *out_sum       = sum;
    if (out_collapsed) *out_collapsed = collapsed;

    sdb_log(SDB_LOG_INFO, "SUM: Aggregation complete. Sum: %.6f, "
            "collapsed: %zu.", sum, collapsed);

    free(buf);
    free(list.uuids);
    return 0;
}

/* ── AVG ───────────────────────────────────────────────────────────── */

int sdb_avg(sdb_t *db, sdb_extractor_fn extractor,
            double *out_avg, size_t *out_collapsed)
{
    if (!db || !extractor)
        return -1;

    uuid_list_t list;
    if (collect_all_uuids(db, &list) != 0)
        return -1;

    double sum = 0.0;
    size_t survived = 0;
    size_t collapsed = 0;

    size_t buf_size = 64 * 1024;
    uint8_t *buf = malloc(buf_size);
    if (!buf) {
        free(list.uuids);
        return -1;
    }

    for (size_t i = 0; i < list.count; i++) {
        size_t payload_len = 0;
        sdb_observe_result_t res = sdb_observe(db, &list.uuids[i],
                                               buf, buf_size, &payload_len);
        if (res == SDB_OBSERVE_OK) {
            sum += extractor(buf, payload_len);
            survived++;
        } else if (res == SDB_OBSERVE_COLLAPSED) {
            collapsed++;
        }
    }

    if (out_avg)
        *out_avg = (survived > 0) ? sum / (double)survived : 0.0;
    if (out_collapsed)
        *out_collapsed = collapsed;

    sdb_log(SDB_LOG_INFO, "AVG: Aggregation complete. Average: %.6f "
            "(from %zu survivors), collapsed: %zu.",
            (survived > 0) ? sum / (double)survived : 0.0,
            survived, collapsed);

    free(buf);
    free(list.uuids);
    return 0;
}
