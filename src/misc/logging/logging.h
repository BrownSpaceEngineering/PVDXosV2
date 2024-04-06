#ifndef LOGIN_H
#define LOGIN_H

#include "globals.h"

/*
FATAL: Worst class of errors that will cause a system restart (e.g. memory corruption, stack overflow, critical function fails)
WARNING: Something unexpected/bad, but recoverable (e.g. failed to take a photo, message checksum invalid)
EVENT: Significant event (e.g. switching power modes, antenna deployed, photo taken, connection established with ground)
INFO: Minor event (e.g. message received, task started, attitude recalculated)
DEBUG: Detailed information about the system for debugging (e.g. length of array is 5, watchdog checked in, mutex locked/unlocked)
*/

// This should never actually execute when building PVDX
// __FILENAME__ is a custom macro defined per-file at compile time, so the IDE doesn't know about it
// To stop the IDE from freaking out, we define it here
#ifndef __FILENAME__
    #if defined(DEVBUILD) || defined(UNITTEST) || defined(RELEASE)
        #error "__FILENAME__ macro should be defined during real builds!"
    #endif
    #define __FILENAME__ "<Filename Resolved at Compile Time>"
#endif

#if defined(DEVBUILD)
    /* Devbuild should include filenames and line numbers */
    #define fatal(msg, ...) fatal_impl("[FATAL|%s:%d]: " msg, __FILENAME__, __LINE__, ##__VA_ARGS__)
    #define warning(msg, ...) warning_impl("[WARNING|%s:%d]: " msg, __FILENAME__, __LINE__, ##__VA_ARGS__)
    #define event(msg, ...) event_impl("[EVENT|%s:%d]: " msg, __FILENAME__, __LINE__, ##__VA_ARGS__)
    #define info(msg, ...) info_impl("[INFO|%s:%d]: " msg, __FILENAME__, __LINE__, ##__VA_ARGS__)
    #define debug(msg, ...) debug_impl("[DEBUG|%s:%d]: " msg, __FILENAME__, __LINE__, ##__VA_ARGS__)
#else
    /* Other build types (such as release or unittest) don't need filenames or line numbers */
    #define fatal(msg, ...) fatal_impl("[FATAL]: " msg, ##__VA_ARGS__)
    #define warning(msg, ...) warning_impl("[WARNING]: " msg, ##__VA_ARGS__)
    #define event(msg, ...) event_impl("[EVENT]: " msg, ##__VA_ARGS__)
    #define info(msg, ...) info_impl("[INFO]: " msg, ##__VA_ARGS__)
    #define debug(msg, ...) debug_impl("[DEBUG]: " msg, ##__VA_ARGS__)
#endif

void fatal_impl(const char *string, ...);
void warning_impl(const char *string, ...);
void event_impl(const char *string, ...);
void info_impl(char *string, ...);
void debug_impl(const char *string, ...);

#endif