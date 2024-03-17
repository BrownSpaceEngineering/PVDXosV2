#ifdef UNITTEST
/*
 * File written by Siddharta Laloux, February 2024
 *
 * Checks that Unity tests are functioning properly on target device
 *
 */

// #include "test_unity_core.c"
#include "../test/unity.h"
// #include "mock-test.h"

int16_t add(int16_t a, int16_t b) { return a + b; }

void setUp(void) {}
void tearDown(void) {}

void test_simple_assert(void) {
    TEST_ASSERT_MESSAGE(3 == 3, "test equal values");
    TEST_ASSERT_MESSAGE(3 == 2, "test unequal values");
}

void test_addition(void) { TEST_ASSERT_EQUAL(5, add(2, 3)); }

void test_addition_failure(void) { TEST_ASSERT_EQUAL(7, add(2, 3)); }

// void run_core_tests(void) { RUN_TEST(test_simple_assert); }

#endif