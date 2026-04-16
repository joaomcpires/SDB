/**
 * volatility.h — Volatility Engine & Quantum-Decoupled Index (QDI)
 *
 * Manages file-backed record storage. The index tracks record existence
 * and file locations without ever reading or verifying record content.
 */

#ifndef SDB_VOLATILITY_H
#define SDB_VOLATILITY_H

#include "hashmap.h"
#include "record.h"

#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/** Index file name within the data directory. */
#define SDB_INDEX_FILENAME ".sdb_index"

/** Maximum data directory path length (leaving room for filenames). */
#define SDB_MAX_DIR_LEN (PATH_MAX - 48)

/**
 * The Volatility Engine.
 *
 * Maintains the QDI (Quantum-Decoupled Index) and manages file-backed
 * record storage. The engine never reads record content during normal
 * operation — it only tracks existence via the index.
 */
typedef struct {
    sdb_hashmap_t *index;                 /**< QDI: UUID → record metadata. */
    char           data_dir[SDB_MAX_DIR_LEN]; /**< Storage directory path. */
} sdb_engine_t;

/**
 * Open (or create) a Volatility Engine backed by the given directory.
 *
 * Creates the data directory if it does not exist.
 * Loads the persisted index if present.
 *
 * @param data_dir  Path to the storage directory.
 * @return Engine handle, or NULL on failure.
 */
sdb_engine_t *sdb_engine_open(const char *data_dir);

/**
 * Close the engine. Persists the current index to disk and frees resources.
 */
void sdb_engine_close(sdb_engine_t *engine);

/**
 * Store a record. Writes it to a new file and updates the QDI.
 *
 * @param engine  Engine handle.
 * @param record  Record to store (payload must be valid).
 * @return 0 on success, -1 on failure.
 */
int sdb_engine_store(sdb_engine_t *engine, const sdb_record_t *record);

/**
 * Locate a record's file path without reading its content.
 *
 * @param engine    Engine handle.
 * @param uuid      Record UUID to locate.
 * @param out_path  Buffer to receive the file path.
 * @param path_len  Size of the out_path buffer.
 * @return 0 on success, -1 if not found.
 */
int sdb_engine_locate(sdb_engine_t *engine, const sdb_uuid_t *uuid,
                      char *out_path, size_t path_len);

/**
 * Remove a record from the QDI (index only — does not erase the file).
 *
 * @return 0 on success, -1 if not found.
 */
int sdb_engine_remove(sdb_engine_t *engine, const sdb_uuid_t *uuid);

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
