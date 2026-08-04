/**
 * src/task/rado/spp.h
 *
 * header file to manage tests
 *
 * Created: 20260201 SUN
 * Authors: Zach Mahan, Siddharta Laloux
 */
#ifndef TESTS_TEST_H
#define TESTS_TEST_H

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
