/**
 * hashmap.c — Open-Addressing Hashmap Implementation
 *
 * Linear probing, power-of-two capacity, FNV-1a hash on UUID bytes.
 * Resizes at 0.75 load factor (counting tombstones).
 */

#include "hashmap.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* ── Hash function (FNV-1a) ────────────────────────────────────────── */

static size_t hash_uuid(const sdb_uuid_t *uuid)
{
    size_t h = 14695981039346656037ULL; /* FNV offset basis */
    for (int i = 0; i < 16; i++) {
        h ^= uuid->bytes[i];
        h *= 1099511628211ULL; /* FNV prime */
    }
    return h;
}

/* ── Helpers ───────────────────────────────────────────────────────── */

static size_t next_power_of_two(size_t n)
{
    size_t p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

/**
 * Probe for a key. Returns the index of:
 *   - the bucket containing the key (if found), or
 *   - the first suitable insertion point (EMPTY or DELETED).
 *
 * Sets *found to true if the key was located.
 */
static size_t probe(const sdb_hashmap_t *map, const sdb_uuid_t *key,
                    bool *found)
{
    size_t mask = map->capacity - 1;
    size_t idx  = hash_uuid(key) & mask;
    size_t first_deleted = (size_t)-1;

    for (size_t i = 0; i < map->capacity; i++) {
        size_t pos = (idx + i) & mask;
        sdb_bucket_t *b = &map->buckets[pos];

        if (b->state == SDB_BUCKET_EMPTY) {
            *found = false;
            return (first_deleted != (size_t)-1) ? first_deleted : pos;
        }

        if (b->state == SDB_BUCKET_DELETED) {
            if (first_deleted == (size_t)-1)
                first_deleted = pos;
            continue;
        }

        /* OCCUPIED */
        if (sdb_uuid_compare(&b->key, key) == 0) {
            *found = true;
            return pos;
        }
    }

    /* Should not happen with a properly sized map */
    *found = false;
    return (first_deleted != (size_t)-1) ? first_deleted : 0;
}

static int resize(sdb_hashmap_t *map, size_t new_capacity)
{
    sdb_bucket_t *old_buckets  = map->buckets;
    size_t        old_capacity = map->capacity;

    map->buckets = calloc(new_capacity, sizeof(sdb_bucket_t));
    if (!map->buckets) {
        map->buckets = old_buckets;
        return -1;
    }

    map->capacity   = new_capacity;
    map->count      = 0;
    map->tombstones = 0;

    for (size_t i = 0; i < old_capacity; i++) {
        if (old_buckets[i].state == SDB_BUCKET_OCCUPIED)
            sdb_hashmap_put(map, &old_buckets[i].key, &old_buckets[i].value);
    }

    free(old_buckets);
    return 0;
}

/* ── Public API ────────────────────────────────────────────────────── */

sdb_hashmap_t *sdb_hashmap_create(size_t initial_capacity)
{
    sdb_hashmap_t *map = calloc(1, sizeof(sdb_hashmap_t));
    if (!map)
        return NULL;

    map->capacity = next_power_of_two(initial_capacity < 16 ? 16 : initial_capacity);
    map->buckets  = calloc(map->capacity, sizeof(sdb_bucket_t));
    if (!map->buckets) {
        free(map);
        return NULL;
    }

    return map;
}

void sdb_hashmap_destroy(sdb_hashmap_t *map)
{
    if (!map)
        return;
    free(map->buckets);
    free(map);
}

int sdb_hashmap_put(sdb_hashmap_t *map, const sdb_uuid_t *key,
                    const sdb_record_meta_t *value)
{
    if (!map || !key || !value) {
        errno = EINVAL;
        return -1;
    }

    /* Resize if load factor (count + tombstones) exceeds 0.75 */
    if ((map->count + map->tombstones) * 4 >= map->capacity * 3) {
        if (resize(map, map->capacity * 2) != 0)
            return -1;
    }

    bool found;
    size_t pos = probe(map, key, &found);

    if (!found) {
        if (map->buckets[pos].state == SDB_BUCKET_DELETED)
            map->tombstones--;
        map->count++;
    }

    map->buckets[pos].state = SDB_BUCKET_OCCUPIED;
    memcpy(&map->buckets[pos].key, key, sizeof(sdb_uuid_t));
    memcpy(&map->buckets[pos].value, value, sizeof(sdb_record_meta_t));

    return 0;
}

sdb_record_meta_t *sdb_hashmap_get(sdb_hashmap_t *map,
                                   const sdb_uuid_t *key)
{
    if (!map || !key)
        return NULL;

    bool found;
    size_t pos = probe(map, key, &found);

    return found ? &map->buckets[pos].value : NULL;
}

int sdb_hashmap_remove(sdb_hashmap_t *map, const sdb_uuid_t *key)
{
    if (!map || !key)
        return -1;

    bool found;
    size_t pos = probe(map, key, &found);

    if (!found)
        return -1;

    map->buckets[pos].state = SDB_BUCKET_DELETED;
    map->count--;
    map->tombstones++;

    return 0;
}

int sdb_hashmap_iterate(sdb_hashmap_t *map, sdb_hashmap_iter_fn fn,
                        void *user_data)
{
    if (!map || !fn)
        return 0;

    for (size_t i = 0; i < map->capacity; i++) {
        if (map->buckets[i].state == SDB_BUCKET_OCCUPIED) {
            int ret = fn(&map->buckets[i].key, &map->buckets[i].value,
                         user_data);
            if (ret != 0)
                return ret;
        }
    }

    return 0;
}

size_t sdb_hashmap_count(const sdb_hashmap_t *map)
{
    return map ? map->count : 0;
}
