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
// does via -DTEST_SPP -DTEST_CFDP. If no test is selected, the defaults below are compiled.
// Destructive hardware MRAM tests require explicit TEST_MRAM selection and are never enabled by default.
// TEST_STXG2 and TEST_STXG2_SWEEP transmit RF, so they also only run when selected by name.
// Keep this list in sync with TEST_NAMES in src/Makefile
#if !defined(TEST_SPP) && !defined(TEST_CFDP) && !defined(TEST_USLP) && !defined(TEST_PMB) &&                     \
    !defined(TEST_TELEMETRY) && !defined(TEST_FLOAT) && !defined(TEST_MRAM) && !defined(TEST_STXG2) &&                    \
    !defined(TEST_STXG2_SWEEP)
    #define TEST_CFDP
    #define TEST_SPP
    #define TEST_USLP
    #define TEST_PMB
    #define TEST_TELEMETRY
    #define TEST_FLOAT
#endif

// The S-band tests build an S-band-only image. main() skips the MRAM burn record, the burn wire, the startup device
// checks, UHF and all tasks. It powers up only the STXG2, runs its POST, then transmits. The image must not touch other
// devices, so no other test can be built with it.
#if defined(TEST_STXG2) || defined(TEST_STXG2_SWEEP)
    #define TEST_SBAND_ONLY_IMAGE
    #if defined(TEST_SPP) || defined(TEST_CFDP) || defined(TEST_USLP) || defined(TEST_PMB) || defined(TEST_TELEMETRY) ||           \
        defined(TEST_FLOAT) || defined(TEST_MRAM) || defined(TEST_LINALG)
        #error "TESTS=stxg2 or stxg2_sweep builds an S-band-only image. Do not select other tests with it."
    #endif
#endif

extern int tests_passed;
extern int tests_total;

#define PVDX_ASSERT_MSG(x, msg)                                                                                                            \
    do {                                                                                                                                   \
        if (!(x)) {                                                                                                                        \
            /* msg is an argument, not pasted into the format, so every assert shares one format string in RAM */                          \
            warning("[!] ASSERT FAILED: %s", msg);                                                                                         \
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
