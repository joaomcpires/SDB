/**
 * record.h — SDB Record Data Structures
 *
 * Defines the core record type, UUID handling, superposition state,
 * and serialization/deserialization routines.
 */

#ifndef SDB_RECORD_H
#define SDB_RECORD_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

/** UUID: 16 raw bytes. */
typedef struct {
    uint8_t bytes[16];
} sdb_uuid_t;

/** Superposition state of a record. */
typedef enum {
    SDB_STATE_POTENTIAL = 0, /**< Unobserved — data may or may not exist. */
    SDB_STATE_STABLE   = 1, /**< Observation collapsed to Stable — data returned. */
    SDB_STATE_UNSTABLE = 2  /**< Observation collapsed to Unstable — data erased. */
} sdb_superposition_state_t;

/**
 * A single SDB record.
 *
 * The payload is heap-allocated and owned by this struct.
 * Use sdb_record_free() to release it.
 */
typedef struct {
    sdb_uuid_t              uuid;
    uint8_t                *payload;
    size_t                  payload_len;
    sdb_superposition_state_t state;
    int64_t                 created_at; /**< Unix timestamp (seconds). */
} sdb_record_t;

/*
 * On-disk binary format (little-endian):
 *
 *   [16 bytes]  uuid
 *   [1  byte ]  state
 *   [8  bytes]  created_at (int64)
 *   [8  bytes]  payload_len (uint64)
 *   [N  bytes]  payload
 *
 * Total header size: 33 bytes.
 */
#define SDB_RECORD_HEADER_SIZE 33

/**
 * Generate a random UUID via /dev/urandom.
 *
 * @param out  Pointer to UUID to fill.
 * @return 0 on success, -1 on failure (errno set).
 */
int sdb_uuid_generate(sdb_uuid_t *out);

/**
 * Format a UUID as a 32-character lowercase hex string (no dashes).
 *
 * @param uuid    The UUID to format.
 * @param buf     Output buffer (must be >= 33 bytes for hex + NUL).
 * @param buf_len Size of the output buffer.
 * @return 0 on success, -1 if buffer too small.
 */
int sdb_uuid_to_string(const sdb_uuid_t *uuid, char *buf, size_t buf_len);

/**
 * Parse a 32-character hex string into a UUID.
 *
 * @param str  Hex string (must be exactly 32 hex chars).
 * @param out  Pointer to UUID to fill.
 * @return 0 on success, -1 on parse error.
 */
int sdb_uuid_from_string(const char *str, sdb_uuid_t *out);

/**
 * Compare two UUIDs.
 *
 * @return 0 if equal, nonzero otherwise.
 */
int sdb_uuid_compare(const sdb_uuid_t *a, const sdb_uuid_t *b);

/**
 * Serialize a record to a byte buffer.
 *
 * @param record  Record to serialize.
 * @param buf     Output buffer (caller-allocated).
 * @param buf_len Size of the output buffer.
 * @return Number of bytes written on success, -1 on failure.
 */
ssize_t sdb_record_serialize(const sdb_record_t *record, uint8_t *buf,
                             size_t buf_len);

/**
 * Deserialize a record from a byte buffer.
 *
 * Allocates memory for the payload. Caller must call sdb_record_free().
 *
 * @param buf      Input buffer.
 * @param buf_len  Size of the input buffer.
 * @param out      Pointer to record to populate.
 * @return 0 on success, -1 on failure.
 */
int sdb_record_deserialize(const uint8_t *buf, size_t buf_len,
                           sdb_record_t *out);

/**
 * Compute the total serialized size of a record.
 */
size_t sdb_record_serialized_size(const sdb_record_t *record);

/**
 * Free heap-allocated payload inside a record.
 * Does not free the record struct itself.
 */
void sdb_record_free(sdb_record_t *record);

#endif /* SDB_RECORD_H */
