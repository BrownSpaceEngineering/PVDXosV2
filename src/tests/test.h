/**
 * src/tests/test.h
 *
 * Minimal on-target test framework (ported from the main branch). Tests are built with
 * `make test` (defines UNITTEST), run from tests_run(), and report over RTT via warning()
 * on failure. PVDX_ASSERT tallies into tests_passed / tests_total.
 */
#ifndef TESTS_TEST_H
#define TESTS_TEST_H

#include "logging.h"

extern int tests_passed;
extern int tests_total;

#define PVDX_ASSERT_MSG(x, msg)                                                                                                            \
    do {                                                                                                                                   \
        if (!(x)) {                                                                                                                        \
            warning("[!] ASSERT FAILED: " msg);                                                                                            \
        } else {                                                                                                                           \
            ++tests_passed;                                                                                                                \
        }                                                                                                                                  \
        ++tests_total;                                                                                                                     \
    } while (0)
#define PVDX_ASSERT(x)                                                                                                                     \
    do {                                                                                                                                   \
        if (!(x)) {                                                                                                                        \
            warning("[!] ASSERT FAILED");                                                                                                  \
        } else {                                                                                                                           \
            ++tests_passed;                                                                                                                \
        }                                                                                                                                  \
        ++tests_total;                                                                                                                     \
    } while (0)

void tests_run(void);

#endif // TESTS_TEST_H
