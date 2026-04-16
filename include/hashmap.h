/**
 * hashmap.h — Open-Addressing Hashmap for QDI
 *
 * Keyed on sdb_uuid_t. Values are sdb_record_meta_t.
 * Uses linear probing with automatic resize at 0.75 load factor.
 */

#ifndef SDB_HASHMAP_H
#define SDB_HASHMAP_H

#include "record.h"

#include <stdbool.h>
#include <stddef.h>

/** Metadata stored in the QDI for each record. */
typedef struct {
    sdb_uuid_t uuid;
    size_t     arena_offset;  /**< Byte offset into the arena file. */
    size_t     record_size;   /**< Total serialized size in the arena. */
    size_t     payload_len;
    int64_t    created_at;
} sdb_record_meta_t;

/** Internal bucket states. */
typedef enum {
    SDB_BUCKET_EMPTY    = 0,
    SDB_BUCKET_OCCUPIED = 1,
    SDB_BUCKET_DELETED  = 2
} sdb_bucket_state_t;

/** A single bucket in the hashmap. */
typedef struct {
    sdb_bucket_state_t state;
    sdb_uuid_t         key;
    sdb_record_meta_t  value;
} sdb_bucket_t;

/** The hashmap. */
typedef struct {
    sdb_bucket_t *buckets;
    size_t        capacity;
    size_t        count;     /**< Number of OCCUPIED entries. */
    size_t        tombstones; /**< Number of DELETED entries. */
} sdb_hashmap_t;

/** Callback type for sdb_hashmap_iterate. */
typedef int (*sdb_hashmap_iter_fn)(const sdb_uuid_t *key,
                                   sdb_record_meta_t *value,
                                   void *user_data);

/**
 * Create a new hashmap with the given initial capacity.
 * Capacity will be rounded up to next power of two.
 *
 * @param initial_capacity  Minimum initial slot count.
 * @return Allocated hashmap, or NULL on failure.
 */
sdb_hashmap_t *sdb_hashmap_create(size_t initial_capacity);

/**
 * Destroy a hashmap and free all memory.
 */
void sdb_hashmap_destroy(sdb_hashmap_t *map);

/**
 * Insert or update a key-value pair.
 *
 * @return 0 on success, -1 on failure.
 */
int sdb_hashmap_put(sdb_hashmap_t *map, const sdb_uuid_t *key,
                    const sdb_record_meta_t *value);

/**
 * Look up a key.
 *
 * @param map  The hashmap.
 * @param key  The UUID key.
 * @return Pointer to the value (valid until next mutation), or NULL.
 */
sdb_record_meta_t *sdb_hashmap_get(sdb_hashmap_t *map,
                                   const sdb_uuid_t *key);

/**
 * Remove a key.
 *
 * @return 0 if removed, -1 if not found.
 */
int sdb_hashmap_remove(sdb_hashmap_t *map, const sdb_uuid_t *key);

/**
 * Iterate over all occupied entries.
 *
 * @param map        The hashmap.
 * @param fn         Callback invoked for each entry. Return 0 to continue,
 *                   nonzero to stop.
 * @param user_data  Opaque pointer passed to the callback.
 * @return 0 if all entries visited, or the nonzero return from fn.
 */
int sdb_hashmap_iterate(sdb_hashmap_t *map, sdb_hashmap_iter_fn fn,
                        void *user_data);

/**
 * Return the number of entries in the hashmap.
 */
size_t sdb_hashmap_count(const sdb_hashmap_t *map);

#endif /* SDB_HASHMAP_H */
