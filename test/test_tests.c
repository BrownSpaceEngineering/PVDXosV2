#ifdef UNITTEST
/*
 * File written by Siddharta Laloux, February 2024
 *
 * Checks that Unity tests are functioning properly on target device
 *
 */

#include "test_unity_core.c"
#include "unity.h"

void run_core_tests(void) {
    return;
    RUN_TEST(testUnitySizeInitializationReminder);
    RUN_TEST(testPassShouldEndImmediatelyWithPass);
    RUN_TEST(testPassShouldEndImmediatelyWithPassAndMessage);
    RUN_TEST(testMessageShouldDisplayMessageWithoutEndingAndGoOnToPass);
    RUN_TEST(testMessageShouldDisplayMessageWithoutEndingAndGoOnToFail);
    RUN_TEST(testTrue);
    RUN_TEST(testFalse);
    RUN_TEST(testSingleStatement);
    RUN_TEST(testPreviousPass);
    RUN_TEST(testNotVanilla);
    RUN_TEST(testNotTrue);
    RUN_TEST(testNotFalse);
    RUN_TEST(testNotUnless);
    RUN_TEST(testNotNotEqual);
    RUN_TEST(testFail);
    RUN_TEST(testIsNull);
    RUN_TEST(testIsNullShouldFailIfNot);
    RUN_TEST(testNotNullShouldFailIfNULL);
    RUN_TEST(testIsEmpty);
    RUN_TEST(testIsEmptyShouldFailIfNot);
    RUN_TEST(testNotEmptyShouldFailIfEmpty);
    RUN_TEST(testIgnore);
    RUN_TEST(testIgnoreMessage);
    RUN_TEST(testProtection);
    RUN_TEST(testIgnoredAndThenFailInTearDown);
    RUN_TEST(testFailureCountIncrementsAndIsReturnedAtEnd);
    RUN_TEST(testThatDetailsCanBeHandleOneDetail);
    RUN_TEST(testThatDetailsCanHandleTestFail);
    RUN_TEST(testThatDetailsCanBeHandleTwoDetails);
    RUN_TEST(testThatDetailsCanBeHandleSingleDetailClearingTwoDetails);
    return;
}

void run_integer_tests(void) {}

void run_memory_tests(void) {}

void test_unity(void) {
    run_core_tests();
    run_integer_tests();
    run_memory_tests();
}

#endif