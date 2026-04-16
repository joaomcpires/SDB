/**
 * test_main.c — SDB Test Runner
 *
 * Single compilation unit that includes all test files.
 * Each test file uses TEST() macro from test_harness.h which
 * auto-registers via GCC constructor attributes.
 */

#include "test_harness.h"

/* Include all test files — each defines TEST() functions */
#include "test_record.c"
#include "test_entropy.c"
#include "test_secure_erase.c"
#include "test_volatility.c"
#include "test_commands.c"
#include "test_aggregate.c"

int main(void)
{
    return run_all_tests();
}
