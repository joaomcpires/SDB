/**
 * test_entropy.c — Entropy Trigger tests
 */

#include "test_harness.h"
#include "entropy.h"

TEST(deterministic_entropy_reproducible)
{
    sdb_entropy_source_t *a = sdb_entropy_deterministic_create(42);
    sdb_entropy_source_t *b = sdb_entropy_deterministic_create(42);
    ASSERT_NE(a, NULL);
    ASSERT_NE(b, NULL);

    for (int i = 0; i < 100; i++) {
        int bit_a = a->collapse_bit(a);
        int bit_b = b->collapse_bit(b);
        ASSERT_EQ(bit_a, bit_b);
        ASSERT_GE(bit_a, 0);
        ASSERT_LE(bit_a, 1);
    }

    sdb_entropy_destroy(a);
    sdb_entropy_destroy(b);
}

TEST(deterministic_entropy_different_seeds)
{
    sdb_entropy_source_t *a = sdb_entropy_deterministic_create(42);
    sdb_entropy_source_t *b = sdb_entropy_deterministic_create(99);
    ASSERT_NE(a, NULL);
    ASSERT_NE(b, NULL);

    int diff_count = 0;
    for (int i = 0; i < 100; i++) {
        int bit_a = a->collapse_bit(a);
        int bit_b = b->collapse_bit(b);
        if (bit_a != bit_b)
            diff_count++;
    }

    /* Different seeds should produce at least some different bits */
    ASSERT_TRUE(diff_count > 0);

    sdb_entropy_destroy(a);
    sdb_entropy_destroy(b);
}

TEST(deterministic_entropy_zero_seed_fails)
{
    sdb_entropy_source_t *src = sdb_entropy_deterministic_create(0);
    ASSERT_EQ(src, NULL);
}

TEST(hardware_entropy_returns_valid_bits)
{
    sdb_entropy_source_t *src = sdb_entropy_hardware_create();
    if (!src) {
        /* /dev/random may not be available in all environments */
        return;
    }

    for (int i = 0; i < 10; i++) {
        int bit = src->collapse_bit(src);
        ASSERT_GE(bit, 0);
        ASSERT_LE(bit, 1);
    }

    sdb_entropy_destroy(src);
}
