/**
 * volatility.c — Volatility Engine Implementation
 *
 * File-backed record storage with a persisted Quantum-Decoupled Index.
 * Strictly adheres to the Non-Verification Protocol: the engine never
 * reads record file content during index operations.
 */

#include "volatility.h"
#include "secure_erase.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Index Persistence ─────────────────────────────────────────────── */

/*
 * Index file format:
 *   [8 bytes]  entry_count (uint64_t LE)
 *   For each entry:
 *     [16 bytes]  uuid
 *     [8  bytes]  payload_len (uint64_t LE)
 *     [8  bytes]  created_at  (int64_t LE)
 *     [2  bytes]  path_len    (uint16_t LE)
 *     [N  bytes]  file_path   (no NUL terminator)
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

static void write_le16(uint8_t *dst, uint16_t val)
{
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)(val >> 8);
}

static uint16_t read_le16(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

/** Write callback for index persistence via iterate. */
typedef struct {
    int fd;
    int error;
} persist_ctx_t;

static int persist_entry(const sdb_uuid_t *key, sdb_record_meta_t *value,
                         void *user_data)
{
    persist_ctx_t *ctx = (persist_ctx_t *)user_data;
    (void)key;

    uint8_t hdr[34]; /* 16 + 8 + 8 + 2 */
    memcpy(hdr, value->uuid.bytes, 16);
    write_le64(hdr + 16, (uint64_t)value->payload_len);
    write_le64(hdr + 24, (uint64_t)value->created_at);

    uint16_t path_len = (uint16_t)strlen(value->file_path);
    write_le16(hdr + 32, path_len);

    if (write(ctx->fd, hdr, 34) != 34) {
        ctx->error = errno;
        return -1;
    }
    if (write(ctx->fd, value->file_path, path_len) != (ssize_t)path_len) {
        ctx->error = errno;
        return -1;
    }

    return 0;
}

int sdb_engine_persist_index(sdb_engine_t *engine)
{
    if (!engine) {
        errno = EINVAL;
        return -1;
    }

    char index_path[PATH_MAX];
    snprintf(index_path, sizeof(index_path), "%s/%s",
             engine->data_dir, SDB_INDEX_FILENAME);

    int fd = open(index_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return -1;

    /* Write entry count */
    uint8_t count_buf[8];
    write_le64(count_buf, (uint64_t)sdb_hashmap_count(engine->index));
    if (write(fd, count_buf, 8) != 8) {
        close(fd);
        return -1;
    }

    /* Write each entry */
    persist_ctx_t ctx = { .fd = fd, .error = 0 };
    int ret = sdb_hashmap_iterate(engine->index, persist_entry, &ctx);

    if (fsync(fd) != 0)
        ret = -1;
    close(fd);

    if (ctx.error) {
        errno = ctx.error;
        return -1;
    }
    return ret;
}

static int load_index(sdb_engine_t *engine)
{
    char index_path[PATH_MAX];
    snprintf(index_path, sizeof(index_path), "%s/%s",
             engine->data_dir, SDB_INDEX_FILENAME);

    int fd = open(index_path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT)
            return 0; /* No index yet — fresh database. */
        return -1;
    }

    /* Read entry count */
    uint8_t count_buf[8];
    if (read(fd, count_buf, 8) != 8) {
        close(fd);
        return -1;
    }
    uint64_t count = read_le64(count_buf);

    for (uint64_t i = 0; i < count; i++) {
        uint8_t hdr[34];
        if (read(fd, hdr, 34) != 34) {
            close(fd);
            return -1;
        }

        sdb_record_meta_t meta;
        memset(&meta, 0, sizeof(meta));
        memcpy(meta.uuid.bytes, hdr, 16);
        meta.payload_len = (size_t)read_le64(hdr + 16);
        meta.created_at  = (int64_t)read_le64(hdr + 24);

        uint16_t path_len = read_le16(hdr + 32);
        if (path_len >= PATH_MAX) {
            close(fd);
            errno = ENAMETOOLONG;
            return -1;
        }
        if (read(fd, meta.file_path, path_len) != (ssize_t)path_len) {
            close(fd);
            return -1;
        }
        meta.file_path[path_len] = '\0';

        if (sdb_hashmap_put(engine->index, &meta.uuid, &meta) != 0) {
            close(fd);
            return -1;
        }
    }

    close(fd);
    return 0;
}

/* ── Engine lifecycle ──────────────────────────────────────────────── */

sdb_engine_t *sdb_engine_open(const char *data_dir)
{
    if (!data_dir) {
        errno = EINVAL;
        return NULL;
    }

    /* Create data directory if needed */
    struct stat st;
    if (stat(data_dir, &st) != 0) {
        if (mkdir(data_dir, 0700) != 0 && errno != EEXIST)
            return NULL;
    }

    sdb_engine_t *engine = calloc(1, sizeof(sdb_engine_t));
    if (!engine)
        return NULL;

    strncpy(engine->data_dir, data_dir, SDB_MAX_DIR_LEN - 1);
    engine->data_dir[SDB_MAX_DIR_LEN - 1] = '\0';

    engine->index = sdb_hashmap_create(64);
    if (!engine->index) {
        free(engine);
        return NULL;
    }

    if (load_index(engine) != 0) {
        sdb_hashmap_destroy(engine->index);
        free(engine);
        return NULL;
    }

    return engine;
}

void sdb_engine_close(sdb_engine_t *engine)
{
    if (!engine)
        return;

    sdb_engine_persist_index(engine);
    sdb_hashmap_destroy(engine->index);
    free(engine);
}

/* ── Record operations ─────────────────────────────────────────────── */

int sdb_engine_store(sdb_engine_t *engine, const sdb_record_t *record)
{
    if (!engine || !record) {
        errno = EINVAL;
        return -1;
    }

    /* Build file path: <data_dir>/<uuid>.sdb */
    char uuid_str[33];
    sdb_uuid_to_string(&record->uuid, uuid_str, sizeof(uuid_str));

    char file_path[PATH_MAX];
    snprintf(file_path, sizeof(file_path), "%s/%s.sdb",
             engine->data_dir, uuid_str);

    /* Serialize record */
    size_t ser_size = sdb_record_serialized_size(record);
    uint8_t *buf = malloc(ser_size);
    if (!buf)
        return -1;

    ssize_t written = sdb_record_serialize(record, buf, ser_size);
    if (written < 0) {
        free(buf);
        return -1;
    }

    /* Write to file */
    int fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        free(buf);
        return -1;
    }

    ssize_t w = write(fd, buf, (size_t)written);
    free(buf);

    if (w != written) {
        close(fd);
        unlink(file_path);
        return -1;
    }

    if (fsync(fd) != 0) {
        close(fd);
        return -1;
    }
    close(fd);

    /* Update QDI */
    sdb_record_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    memcpy(&meta.uuid, &record->uuid, sizeof(sdb_uuid_t));
    strncpy(meta.file_path, file_path, PATH_MAX - 1);
    meta.payload_len = record->payload_len;
    meta.created_at  = record->created_at;

    if (sdb_hashmap_put(engine->index, &record->uuid, &meta) != 0)
        return -1;

    /* Persist index after mutation */
    return sdb_engine_persist_index(engine);
}

int sdb_engine_locate(sdb_engine_t *engine, const sdb_uuid_t *uuid,
                      char *out_path, size_t path_len)
{
    if (!engine || !uuid || !out_path) {
        errno = EINVAL;
        return -1;
    }

    sdb_record_meta_t *meta = sdb_hashmap_get(engine->index, uuid);
    if (!meta) {
        errno = ENOENT;
        return -1;
    }

    strncpy(out_path, meta->file_path, path_len - 1);
    out_path[path_len - 1] = '\0';
    return 0;
}

int sdb_engine_remove(sdb_engine_t *engine, const sdb_uuid_t *uuid)
{
    if (!engine || !uuid) {
        errno = EINVAL;
        return -1;
    }

    if (sdb_hashmap_remove(engine->index, uuid) != 0) {
        errno = ENOENT;
        return -1;
    }

    /* Persist index after mutation */
    return sdb_engine_persist_index(engine);
}

size_t sdb_engine_count(const sdb_engine_t *engine)
{
    return engine ? sdb_hashmap_count(engine->index) : 0;
}

sdb_hashmap_t *sdb_engine_get_index(sdb_engine_t *engine)
{
    return engine ? engine->index : NULL;
}
