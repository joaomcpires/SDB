/**
 * volatility.h — Volatility Engine & Quantum-Decoupled Index (QDI)
 *
 * Manages mmap'd arena-backed record storage. The index tracks record
 * existence and arena offsets without ever reading or verifying record
 * content.
 */

#ifndef SDB_VOLATILITY_H
#define SDB_VOLATILITY_H

#include "hashmap.h"
#include "record.h"

#include <limits.h>
#include <stdint.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/** Index file name within the data directory. */
#define SDB_INDEX_FILENAME ".sdb_index"

/** Arena file name within the data directory. */
#define SDB_ARENA_FILENAME "sdb_arena.dat"

/** Maximum data directory path length (leaving room for filenames). */
#define SDB_MAX_DIR_LEN (PATH_MAX - 48)

/** Initial arena size (4 MiB). */
#define SDB_ARENA_INITIAL_SIZE (4u * 1024u * 1024u)

/** Arena growth increment (4 MiB). */
#define SDB_ARENA_GROW_INCREMENT (4u * 1024u * 1024u)

/** Minimum chunk size in the freelist (avoid tiny fragments). */
#define SDB_ARENA_MIN_CHUNK 64

/** Sentinel value indicating end of freelist. */
#define SDB_FREELIST_END ((size_t)-1)

/**
 * Arena file header (stored at offset 0).
 *
 *   [8 bytes]  magic ("SDB_AREN")
 *   [8 bytes]  arena_size (total mapped size)
 *   [8 bytes]  arena_used (high-water mark for allocation)
 *   [8 bytes]  free_list_head (offset of first free chunk, or SDB_FREELIST_END)
 */
#define SDB_ARENA_HEADER_SIZE 32
#define SDB_ARENA_MAGIC "SDB_AREN"

/**
 * Free chunk header (in-band, stored inside the arena at free regions).
 *
 *   [8 bytes]  chunk_size  (total chunk size including this header)
 *   [8 bytes]  next_free   (offset of next free chunk, or SDB_FREELIST_END)
 */
#define SDB_FREE_CHUNK_HEADER_SIZE 16

/**
 * The Volatility Engine.
 *
 * Maintains the QDI (Quantum-Decoupled Index) and manages mmap'd
 * arena-backed record storage. The engine never reads record content
 * during normal operation — it only tracks existence via the index.
 */
typedef struct {
    sdb_hashmap_t *index;                     /**< QDI: UUID → record metadata. */
    char           data_dir[SDB_MAX_DIR_LEN]; /**< Storage directory path. */
    int            arena_fd;                  /**< File descriptor for the arena. */
    uint8_t       *arena_map;                 /**< mmap'd pointer to the arena. */
    size_t         arena_size;                /**< Current mapped size. */
    size_t         arena_used;                /**< High-water mark (next bump alloc). */
    size_t         free_list_head;            /**< Head of the in-arena freelist. */
} sdb_engine_t;

/**
 * Open (or create) a Volatility Engine backed by the given directory.
 *
 * Creates the data directory if it does not exist.
 * Opens or creates the arena file and mmaps it.
 * Loads the persisted index if present.
 *
 * @param data_dir  Path to the storage directory.
 * @return Engine handle, or NULL on failure.
 */
sdb_engine_t *sdb_engine_open(const char *data_dir);

/**
 * Close the engine. Persists the current index to disk, syncs and
 * unmaps the arena, and frees resources.
 */
void sdb_engine_close(sdb_engine_t *engine);

/**
 * Store a record. Allocates space in the arena and updates the QDI.
 *
 * @param engine  Engine handle.
 * @param record  Record to store (payload must be valid).
 * @return 0 on success, -1 on failure.
 */
int sdb_engine_store(sdb_engine_t *engine, const sdb_record_t *record);

/**
 * Get a read-only pointer to a record's data in the arena.
 *
 * @param engine    Engine handle.
 * @param uuid      Record UUID to locate.
 * @param out_ptr   Receives pointer into the arena (valid until arena remap).
 * @param out_size  Receives the serialized record size.
 * @return 0 on success, -1 if not found.
 */
int sdb_engine_locate(sdb_engine_t *engine, const sdb_uuid_t *uuid,
                      const uint8_t **out_ptr, size_t *out_size);

/**
 * Remove a record from the QDI and free its arena space.
 *
 * @return 0 on success, -1 if not found.
 */
int sdb_engine_remove(sdb_engine_t *engine, const sdb_uuid_t *uuid);

/**
 * Securely erase a record's arena region (zero-fill + msync),
 * then remove from the QDI.
 *
 * @return 0 on success, -1 if not found.
 */
int sdb_engine_erase(sdb_engine_t *engine, const sdb_uuid_t *uuid);

/**
 * Persist the current index to disk.
 *
 * @return 0 on success, -1 on failure.
 */
int sdb_engine_persist_index(sdb_engine_t *engine);

/**
 * Get the number of records tracked by the engine.
 */
size_t sdb_engine_count(const sdb_engine_t *engine);

/**
 * Get the underlying hashmap for direct iteration.
 */
sdb_hashmap_t *sdb_engine_get_index(sdb_engine_t *engine);

#endif /* SDB_VOLATILITY_H */
