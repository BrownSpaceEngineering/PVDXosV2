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

#define PVDX_ASSERT_MSG(x, msg)                                                                                                            \
    do {                                                                                                                                   \
        if (!(x)) {                                                                                                                        \
            warning("[!] ASSERT FAILED: " msg);                                                                                            \
        }                                                                                                                                  \
    } while (0)
#define PVDX_ASSERT(x)                                                                                                                     \
    do {                                                                                                                                   \
        if (!(x)) {                                                                                                                        \
            warning("[!] ASSERT FAILED");                                                                                                  \
        }                                                                                                                                  \
    } while (0)
void tests_run(void);

#endif
