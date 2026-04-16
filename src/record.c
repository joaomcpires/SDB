/**
 * record.c — SDB Record Implementation
 *
 * UUID generation, serialization, deserialization, and comparison.
 */

#include "record.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── UUID ──────────────────────────────────────────────────────────── */

int sdb_uuid_generate(sdb_uuid_t *out)
{
    if (!out) {
        errno = EINVAL;
        return -1;
    }

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return -1;

    ssize_t n = read(fd, out->bytes, sizeof(out->bytes));
    close(fd);

    if (n != (ssize_t)sizeof(out->bytes))
        return -1;

    return 0;
}

int sdb_uuid_to_string(const sdb_uuid_t *uuid, char *buf, size_t buf_len)
{
    if (!uuid || !buf || buf_len < 33) {
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; i < 16; i++)
        snprintf(buf + i * 2, 3, "%02x", uuid->bytes[i]);

    buf[32] = '\0';
    return 0;
}

int sdb_uuid_from_string(const char *str, sdb_uuid_t *out)
{
    if (!str || !out || strlen(str) != 32) {
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; i < 16; i++) {
        unsigned int val;
        if (sscanf(str + i * 2, "%2x", &val) != 1)
            return -1;
        out->bytes[i] = (uint8_t)val;
    }

    return 0;
}

int sdb_uuid_compare(const sdb_uuid_t *a, const sdb_uuid_t *b)
{
    if (!a || !b)
        return a != b; /* both NULL => equal, one NULL => not equal */
    return memcmp(a->bytes, b->bytes, 16);
}

/* ── Serialization ─────────────────────────────────────────────────── */

/*
 * Wire format (little-endian):
 *   [16]  uuid
 *   [ 1]  state
 *   [ 8]  created_at  (int64_t)
 *   [ 8]  payload_len (uint64_t)
 *   [ N]  payload
 */

static void write_le64(uint8_t *dst, uint64_t val)
{
    for (int i = 0; i < 8; i++)
        dst[i] = (uint8_t)(val >> (i * 8));
}

static uint64_t read_le64(const uint8_t *src)
{
    uint64_t val = 0;
    for (int i = 0; i < 8; i++)
        val |= (uint64_t)src[i] << (i * 8);
    return val;
}

size_t sdb_record_serialized_size(const sdb_record_t *record)
{
    return SDB_RECORD_HEADER_SIZE + record->payload_len;
}

ssize_t sdb_record_serialize(const sdb_record_t *record, uint8_t *buf,
                             size_t buf_len)
{
    if (!record || !buf) {
        errno = EINVAL;
        return -1;
    }

    size_t total = sdb_record_serialized_size(record);
    if (buf_len < total) {
        errno = ENOSPC;
        return -1;
    }

    size_t off = 0;

    /* uuid */
    memcpy(buf + off, record->uuid.bytes, 16);
    off += 16;

    /* state */
    buf[off++] = (uint8_t)record->state;

    /* created_at */
    write_le64(buf + off, (uint64_t)record->created_at);
    off += 8;

    /* payload_len */
    write_le64(buf + off, (uint64_t)record->payload_len);
    off += 8;

    /* payload */
    if (record->payload_len > 0 && record->payload)
        memcpy(buf + off, record->payload, record->payload_len);
    off += record->payload_len;

    return (ssize_t)off;
}

int sdb_record_deserialize(const uint8_t *buf, size_t buf_len,
                           sdb_record_t *out)
{
    if (!buf || !out) {
        errno = EINVAL;
        return -1;
    }

    if (buf_len < SDB_RECORD_HEADER_SIZE) {
        errno = ENODATA;
        return -1;
    }

    size_t off = 0;

    /* uuid */
    memcpy(out->uuid.bytes, buf + off, 16);
    off += 16;

    /* state */
    out->state = (sdb_superposition_state_t)buf[off++];

    /* created_at */
    out->created_at = (int64_t)read_le64(buf + off);
    off += 8;

    /* payload_len */
    out->payload_len = (size_t)read_le64(buf + off);
    off += 8;

    /* Validate remaining buffer */
    if (buf_len < SDB_RECORD_HEADER_SIZE + out->payload_len) {
        errno = ENODATA;
        return -1;
    }

    /* payload */
    if (out->payload_len > 0) {
        out->payload = malloc(out->payload_len);
        if (!out->payload) {
            errno = ENOMEM;
            return -1;
        }
        memcpy(out->payload, buf + off, out->payload_len);
    } else {
        out->payload = NULL;
    }

    return 0;
}

/* ── Cleanup ───────────────────────────────────────────────────────── */

void sdb_record_free(sdb_record_t *record)
{
    if (record && record->payload) {
        free(record->payload);
        record->payload = NULL;
        record->payload_len = 0;
    }
}
