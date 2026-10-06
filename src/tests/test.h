/**
 * src/tests/test.h
 *
 * header file to manage tests
 *
 * Created: 20260201 SUN
 * Authors: Zach Mahan, Siddharta Laloux
 */
#ifndef TESTS_TEST_H
#define TESTS_TEST_H

// Each test is only compiled when its TEST_<NAME> macro is defined, which `make test TESTS="spp cfdp"`
// does via -DTEST_SPP -DTEST_CFDP. If no test is selected, all of them are compiled.
// Keep this list in sync with TEST_NAMES in src/Makefile
#if !defined(TEST_SPP) && !defined(TEST_LINALG) && !defined(TEST_CFDP) && !defined(TEST_USLP) && !defined(TEST_TELEMETRY)
    #define TEST_SPP
    #define TEST_LINALG
    #define TEST_CFDP
    #define TEST_USLP
    #define TEST_TELEMETRY
#endif

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

#endif
