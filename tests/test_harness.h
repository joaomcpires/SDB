/**
 * test_harness.h — SDB Test Framework
 *
 * Minimal test runner with assertion macros and auto-registration.
 */

#ifndef SDB_TEST_HARNESS_H
#define SDB_TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Assertion macros ──────────────────────────────────────────────── */

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define ASSERT_TRUE(expr)                                                  \
    do {                                                                   \
        g_tests_run++;                                                     \
        if (!(expr)) {                                                     \
            fprintf(stderr, "  FAIL: %s:%d: %s\n",                        \
                    __FILE__, __LINE__, #expr);                            \
            g_tests_failed++;                                              \
            return;                                                        \
        } else {                                                           \
            g_tests_passed++;                                              \
        }                                                                  \
    } while (0)

#define ASSERT_EQ(a, b)   ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b)   ASSERT_TRUE((a) != (b))
#define ASSERT_GE(a, b)   ASSERT_TRUE((a) >= (b))
#define ASSERT_LE(a, b)   ASSERT_TRUE((a) <= (b))
#define ASSERT_STR_EQ(a, b) ASSERT_TRUE(strcmp((a), (b)) == 0)

/* ── Test registration ─────────────────────────────────────────────── */

typedef void (*test_fn)(void);

typedef struct {
    const char *name;
    test_fn     fn;
} test_case_t;

#define MAX_TESTS 256
static test_case_t g_test_cases[MAX_TESTS];
static int g_test_count = 0;

static inline void register_test(const char *name, test_fn fn)
{
    if (g_test_count < MAX_TESTS) {
        g_test_cases[g_test_count].name = name;
        g_test_cases[g_test_count].fn   = fn;
        g_test_count++;
    }
}

#define TEST(name)                                                         \
    static void test_##name(void);                                         \
    __attribute__((constructor)) static void register_##name(void) {       \
        register_test(#name, test_##name);                                 \
    }                                                                      \
    static void test_##name(void)

/* ── Runner ────────────────────────────────────────────────────────── */

static inline int run_all_tests(void)
{
    printf("SDB Test Suite — %d tests registered\n", g_test_count);
    printf("─────────────────────────────────────\n");

    for (int i = 0; i < g_test_count; i++) {
        int before_failed = g_tests_failed;
        printf("  [RUN ] %s\n", g_test_cases[i].name);
        g_test_cases[i].fn();
        if (g_tests_failed == before_failed)
            printf("  [ OK ] %s\n", g_test_cases[i].name);
        else
            printf("  [FAIL] %s\n", g_test_cases[i].name);
    }

    printf("─────────────────────────────────────\n");
    printf("Results: %d/%d passed, %d failed\n",
           g_tests_passed, g_tests_run, g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}

/* ── Shared test helpers ───────────────────────────────────────────── */

static inline void cleanup_dir(const char *dir)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    (void)system(cmd);
}

#endif /* SDB_TEST_HARNESS_H */
