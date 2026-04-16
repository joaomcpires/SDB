/**
 * volatility.c — Volatility Engine Implementation
 *
 * mmap'd arena-backed record storage with a persisted Quantum-Decoupled
 * Index. Strictly adheres to the Non-Verification Protocol: the engine
 * never reads record file content during index operations.
 *
 * Arena layout:
 *   [32 bytes]  Header (magic, arena_size, arena_used, free_list_head)
 *   [N  bytes]  Record data / free chunks
 */

#define _GNU_SOURCE
#include "volatility.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Little-endian helpers ─────────────────────────────────────────── */

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

/* ── Arena header I/O ──────────────────────────────────────────────── */

static void arena_write_header(sdb_engine_t *engine)
{
    uint8_t *hdr = engine->arena_map;
    memcpy(hdr, SDB_ARENA_MAGIC, 8);
    write_le64(hdr + 8,  (uint64_t)engine->arena_size);
    write_le64(hdr + 16, (uint64_t)engine->arena_used);
    write_le64(hdr + 24, (uint64_t)engine->free_list_head);
}

static int arena_read_header(sdb_engine_t *engine)
{
    uint8_t *hdr = engine->arena_map;
    if (memcmp(hdr, SDB_ARENA_MAGIC, 8) != 0) {
        errno = EINVAL;
        return -1;
    }
    engine->arena_size     = (size_t)read_le64(hdr + 8);
    engine->arena_used     = (size_t)read_le64(hdr + 16);
    engine->free_list_head = (size_t)read_le64(hdr + 24);
    return 0;
}

/* ── Arena allocation ──────────────────────────────────────────────── */

/**
 * Grow the arena by at least `min_extra` bytes.
 * Uses ftruncate + mremap for incremental growth.
 */
static int arena_grow(sdb_engine_t *engine, size_t min_extra)
{
    size_t grow = SDB_ARENA_GROW_INCREMENT;
    if (grow < min_extra)
        grow = min_extra;

    /* Round up to page size */
    long page = sysconf(_SC_PAGESIZE);
    if (page > 0)
        grow = (grow + (size_t)page - 1) & ~((size_t)page - 1);

    size_t new_size = engine->arena_size + grow;

    if (ftruncate(engine->arena_fd, (off_t)new_size) != 0)
        return -1;

    uint8_t *new_map = (uint8_t *)mremap(engine->arena_map, engine->arena_size,
                                          new_size, MREMAP_MAYMOVE);
    if (new_map == MAP_FAILED)
        return -1;

    engine->arena_map  = new_map;
    engine->arena_size = new_size;

    /* Update header with new size */
    arena_write_header(engine);

    return 0;
}

/**
 * Allocate `size` bytes from the arena.
 * First-fit scan of the freelist, then bump allocation, then grow.
 *
 * @return Offset into the arena, or (size_t)-1 on failure.
 */
static size_t arena_alloc(sdb_engine_t *engine, size_t size)
{
    /* Enforce minimum chunk size */
    if (size < SDB_ARENA_MIN_CHUNK)
        size = SDB_ARENA_MIN_CHUNK;

    /* First-fit free list scan */
    size_t prev_off = SDB_FREELIST_END;
    size_t cur_off  = engine->free_list_head;

    while (cur_off != SDB_FREELIST_END) {
        uint8_t *chunk = engine->arena_map + cur_off;
        size_t chunk_size = (size_t)read_le64(chunk);
        size_t next_free  = (size_t)read_le64(chunk + 8);

        if (chunk_size >= size) {
            size_t remainder = chunk_size - size;

            if (remainder >= SDB_ARENA_MIN_CHUNK + SDB_FREE_CHUNK_HEADER_SIZE) {
                /* Split: shrink this chunk, create new free chunk */
                size_t new_free_off = cur_off + size;
                uint8_t *new_free = engine->arena_map + new_free_off;
                write_le64(new_free, (uint64_t)remainder);
                write_le64(new_free + 8, (uint64_t)next_free);

                /* Update prev to point to new free chunk */
                if (prev_off == SDB_FREELIST_END)
                    engine->free_list_head = new_free_off;
                else
                    write_le64(engine->arena_map + prev_off + 8,
                               (uint64_t)new_free_off);
            } else {
                /* Use entire chunk (absorb small remainder) */
                size = chunk_size; /* caller gets slightly more */
                if (prev_off == SDB_FREELIST_END)
                    engine->free_list_head = next_free;
                else
                    write_le64(engine->arena_map + prev_off + 8,
                               (uint64_t)next_free);
            }

            arena_write_header(engine);
            return cur_off;
        }

        prev_off = cur_off;
        cur_off  = next_free;
    }

    /* Bump allocation */
    if (engine->arena_used + size <= engine->arena_size) {
        size_t off = engine->arena_used;
        engine->arena_used += size;
        arena_write_header(engine);
        return off;
    }

    /* Need to grow */
    if (arena_grow(engine, size) != 0)
        return SDB_FREELIST_END;

    size_t off = engine->arena_used;
    engine->arena_used += size;
    arena_write_header(engine);
    return off;
}

/**
 * Return `size` bytes at `offset` to the freelist.
 * Inserts at head (O(1)).
 */
static void arena_free(sdb_engine_t *engine, size_t offset, size_t size)
{
    if (size < SDB_FREE_CHUNK_HEADER_SIZE)
        return; /* Too small to track */

    uint8_t *chunk = engine->arena_map + offset;
    write_le64(chunk, (uint64_t)size);
    write_le64(chunk + 8, (uint64_t)engine->free_list_head);
    engine->free_list_head = offset;

    arena_write_header(engine);
}

/* ── Index Persistence ─────────────────────────────────────────────── */

/*
 * Index file format:
 *   [8 bytes]  entry_count (uint64_t LE)
 *   For each entry:
 *     [16 bytes]  uuid
 *     [8  bytes]  arena_offset  (uint64_t LE)
 *     [8  bytes]  record_size   (uint64_t LE)
 *     [8  bytes]  payload_len   (uint64_t LE)
 *     [8  bytes]  created_at    (int64_t LE)
 */
#define SDB_INDEX_ENTRY_SIZE (16 + 8 + 8 + 8 + 8)

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

    uint8_t buf[SDB_INDEX_ENTRY_SIZE];
    memcpy(buf, value->uuid.bytes, 16);
    write_le64(buf + 16, (uint64_t)value->arena_offset);
    write_le64(buf + 24, (uint64_t)value->record_size);
    write_le64(buf + 32, (uint64_t)value->payload_len);
    write_le64(buf + 40, (uint64_t)value->created_at);

    if (write(ctx->fd, buf, SDB_INDEX_ENTRY_SIZE) !=
        (ssize_t)SDB_INDEX_ENTRY_SIZE) {
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
        uint8_t buf[SDB_INDEX_ENTRY_SIZE];
        if (read(fd, buf, SDB_INDEX_ENTRY_SIZE) !=
            (ssize_t)SDB_INDEX_ENTRY_SIZE) {
            close(fd);
            return -1;
        }

        sdb_record_meta_t meta;
        memset(&meta, 0, sizeof(meta));
        memcpy(meta.uuid.bytes, buf, 16);
        meta.arena_offset = (size_t)read_le64(buf + 16);
        meta.record_size  = (size_t)read_le64(buf + 24);
        meta.payload_len  = (size_t)read_le64(buf + 32);
        meta.created_at   = (int64_t)read_le64(buf + 40);

        if (sdb_hashmap_put(engine->index, &meta.uuid, &meta) != 0) {
            close(fd);
            return -1;
        }
    }

    close(fd);
    return 0;
}

/* ── Arena setup ───────────────────────────────────────────────────── */

static int arena_open(sdb_engine_t *engine)
{
    char arena_path[PATH_MAX];
    snprintf(arena_path, sizeof(arena_path), "%s/%s",
             engine->data_dir, SDB_ARENA_FILENAME);

    int created = 0;
    engine->arena_fd = open(arena_path, O_RDWR | O_CREAT, 0600);
    if (engine->arena_fd < 0)
        return -1;

    struct stat st;
    if (fstat(engine->arena_fd, &st) != 0) {
        close(engine->arena_fd);
        return -1;
    }

    if (st.st_size < (off_t)SDB_ARENA_HEADER_SIZE) {
        /* New or corrupt arena — initialize */
        created = 1;
        size_t init_size = SDB_ARENA_INITIAL_SIZE;
        if (ftruncate(engine->arena_fd, (off_t)init_size) != 0) {
            close(engine->arena_fd);
            return -1;
        }
        engine->arena_size = init_size;
    } else {
        engine->arena_size = (size_t)st.st_size;
    }

    engine->arena_map = (uint8_t *)mmap(NULL, engine->arena_size,
                                         PROT_READ | PROT_WRITE,
                                         MAP_SHARED, engine->arena_fd, 0);
    if (engine->arena_map == MAP_FAILED) {
        close(engine->arena_fd);
        return -1;
    }

    if (created) {
        engine->arena_used     = SDB_ARENA_HEADER_SIZE;
        engine->free_list_head = SDB_FREELIST_END;
        arena_write_header(engine);
        msync(engine->arena_map, SDB_ARENA_HEADER_SIZE, MS_SYNC);
    } else {
        if (arena_read_header(engine) != 0) {
            munmap(engine->arena_map, engine->arena_size);
            close(engine->arena_fd);
            return -1;
        }
    }

    return 0;
}

static void arena_close(sdb_engine_t *engine)
{
    if (engine->arena_map && engine->arena_map != MAP_FAILED) {
        arena_write_header(engine);
        msync(engine->arena_map, engine->arena_size, MS_SYNC);
        munmap(engine->arena_map, engine->arena_size);
    }
    if (engine->arena_fd >= 0)
        close(engine->arena_fd);
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

    engine->arena_fd = -1;
    strncpy(engine->data_dir, data_dir, SDB_MAX_DIR_LEN - 1);
    engine->data_dir[SDB_MAX_DIR_LEN - 1] = '\0';

    engine->index = sdb_hashmap_create(64);
    if (!engine->index) {
        free(engine);
        return NULL;
    }

    if (arena_open(engine) != 0) {
        sdb_hashmap_destroy(engine->index);
        free(engine);
        return NULL;
    }

    if (load_index(engine) != 0) {
        arena_close(engine);
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
    arena_close(engine);
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

    /* Serialize record to temp buffer */
    size_t ser_size = sdb_record_serialized_size(record);
    uint8_t *buf = malloc(ser_size);
    if (!buf)
        return -1;

    ssize_t written = sdb_record_serialize(record, buf, ser_size);
    if (written < 0) {
        free(buf);
        return -1;
    }

    /* Allocate space in arena */
    size_t offset = arena_alloc(engine, (size_t)written);
    if (offset == SDB_FREELIST_END) {
        free(buf);
        return -1;
    }

    /* Copy serialized data into arena */
    memcpy(engine->arena_map + offset, buf, (size_t)written);
    free(buf);

    /* Sync the written region */
    msync(engine->arena_map + offset, (size_t)written, MS_SYNC);

    /* Update QDI */
    sdb_record_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    memcpy(&meta.uuid, &record->uuid, sizeof(sdb_uuid_t));
    meta.arena_offset = offset;
    meta.record_size  = (size_t)written;
    meta.payload_len  = record->payload_len;
    meta.created_at   = record->created_at;

    if (sdb_hashmap_put(engine->index, &record->uuid, &meta) != 0)
        return -1;

    /* Persist index after mutation */
    return sdb_engine_persist_index(engine);
}

int sdb_engine_locate(sdb_engine_t *engine, const sdb_uuid_t *uuid,
                      const uint8_t **out_ptr, size_t *out_size)
{
    if (!engine || !uuid) {
        errno = EINVAL;
        return -1;
    }

    sdb_record_meta_t *meta = sdb_hashmap_get(engine->index, uuid);
    if (!meta) {
        errno = ENOENT;
        return -1;
    }

    if (out_ptr)
        *out_ptr = engine->arena_map + meta->arena_offset;
    if (out_size)
        *out_size = meta->record_size;

    return 0;
}

int sdb_engine_remove(sdb_engine_t *engine, const sdb_uuid_t *uuid)
{
    if (!engine || !uuid) {
        errno = EINVAL;
        return -1;
    }

    sdb_record_meta_t *meta = sdb_hashmap_get(engine->index, uuid);
    if (!meta) {
        errno = ENOENT;
        return -1;
    }

    /* Return arena space to freelist */
    arena_free(engine, meta->arena_offset, meta->record_size);

    if (sdb_hashmap_remove(engine->index, uuid) != 0) {
        errno = ENOENT;
        return -1;
    }

    /* Persist index after mutation */
    return sdb_engine_persist_index(engine);
}

int sdb_engine_erase(sdb_engine_t *engine, const sdb_uuid_t *uuid)
{
    if (!engine || !uuid) {
        errno = EINVAL;
        return -1;
    }

    sdb_record_meta_t *meta = sdb_hashmap_get(engine->index, uuid);
    if (!meta) {
        errno = ENOENT;
        return -1;
    }

    /* Zero-fill the arena region (secure erase) */
    memset(engine->arena_map + meta->arena_offset, 0, meta->record_size);
    msync(engine->arena_map + meta->arena_offset, meta->record_size, MS_SYNC);

    /* Return to freelist */
    arena_free(engine, meta->arena_offset, meta->record_size);

    if (sdb_hashmap_remove(engine->index, uuid) != 0) {
        errno = ENOENT;
        return -1;
    }

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
