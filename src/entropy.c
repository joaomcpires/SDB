/**
 * entropy.c — Entropy Trigger Implementation
 *
 * Hardware source reads single bytes from /dev/random and extracts the LSB.
 * Deterministic source uses xorshift64 for reproducible test sequences.
 */

#include "entropy.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

/* ── Hardware Entropy ──────────────────────────────────────────────── */

typedef struct {
    int fd; /**< File descriptor for /dev/random. */
} hw_entropy_t;

static int hw_collapse_bit(sdb_entropy_source_t *self)
{
    hw_entropy_t *hw = (hw_entropy_t *)self->impl_data;
    uint8_t byte;

    ssize_t n = read(hw->fd, &byte, 1);
    if (n != 1)
        return -1;

    return byte & 1; /* LSB: 0 = Stable, 1 = Unstable */
}

sdb_entropy_source_t *sdb_entropy_hardware_create(void)
{
    sdb_entropy_source_t *src = calloc(1, sizeof(sdb_entropy_source_t));
    if (!src)
        return NULL;

    hw_entropy_t *hw = calloc(1, sizeof(hw_entropy_t));
    if (!hw) {
        free(src);
        return NULL;
    }

    hw->fd = open("/dev/random", O_RDONLY);
    if (hw->fd < 0) {
        free(hw);
        free(src);
        return NULL;
    }

    src->collapse_bit = hw_collapse_bit;
    src->impl_data    = hw;
    return src;
}

/* ── Deterministic Entropy (xorshift64) ────────────────────────────── */

typedef struct {
    uint64_t state;
} det_entropy_t;

static uint64_t xorshift64(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static int det_collapse_bit(sdb_entropy_source_t *self)
{
    det_entropy_t *det = (det_entropy_t *)self->impl_data;
    uint64_t val = xorshift64(&det->state);
    return (int)(val & 1);
}

sdb_entropy_source_t *sdb_entropy_deterministic_create(uint64_t seed)
{
    if (seed == 0) {
        errno = EINVAL;
        return NULL;
    }

    sdb_entropy_source_t *src = calloc(1, sizeof(sdb_entropy_source_t));
    if (!src)
        return NULL;

    det_entropy_t *det = calloc(1, sizeof(det_entropy_t));
    if (!det) {
        free(src);
        return NULL;
    }

    det->state = seed;
    src->collapse_bit = det_collapse_bit;
    src->impl_data    = det;
    return src;
}

/* ── Cleanup ───────────────────────────────────────────────────────── */

void sdb_entropy_destroy(sdb_entropy_source_t *source)
{
    if (!source)
        return;

    if (source->collapse_bit == hw_collapse_bit) {
        hw_entropy_t *hw = (hw_entropy_t *)source->impl_data;
        if (hw) {
            close(hw->fd);
            free(hw);
        }
    } else {
        free(source->impl_data);
    }

    free(source);
}
