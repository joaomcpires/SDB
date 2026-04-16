/**
 * entropy.h — Entropy Trigger Interface
 *
 * Provides the probabilistic collapse mechanism. Two implementations:
 *   - Hardware: reads from /dev/random (true randomness).
 *   - Deterministic: xorshift64 PRNG for reproducible testing.
 */

#ifndef SDB_ENTROPY_H
#define SDB_ENTROPY_H

#include <stdint.h>

/** Forward declaration. */
typedef struct sdb_entropy_source sdb_entropy_source_t;

/**
 * Collapse bit function signature.
 *
 * @param self  The entropy source.
 * @return 0 = Stable (return data), 1 = Unstable (erase), -1 = error.
 */
typedef int (*sdb_collapse_fn)(sdb_entropy_source_t *self);

/**
 * Entropy source structure.
 * Implementations store their state after this header.
 */
struct sdb_entropy_source {
    sdb_collapse_fn collapse_bit;
    void           *impl_data; /**< Implementation-specific state. */
};

/**
 * Create a hardware entropy source backed by /dev/random.
 *
 * @return Allocated source, or NULL on failure.
 */
sdb_entropy_source_t *sdb_entropy_hardware_create(void);

/**
 * Create a deterministic entropy source using xorshift64.
 *
 * @param seed  Initial PRNG seed. Must be nonzero.
 * @return Allocated source, or NULL on failure.
 */
sdb_entropy_source_t *sdb_entropy_deterministic_create(uint64_t seed);

/**
 * Destroy an entropy source and free resources.
 */
void sdb_entropy_destroy(sdb_entropy_source_t *source);

#endif /* SDB_ENTROPY_H */
