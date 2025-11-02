#ifndef CAMERA_TESTS_H
#define CAMERA_TESTS_H

#include <stdint.h>

typedef struct {
    const char *name;
    int (*test_func)(void);
} camera_test_entry_t;

// Exposed API for shell integration
void camera_tests_list(void);
// Returns: number of tests passed
int camera_tests_run_all(int *out_total);
// Returns: 0 on success, nonzero on failure or not found
int camera_tests_run_one(const char *name);

#endif // CAMERA_TESTS_H

