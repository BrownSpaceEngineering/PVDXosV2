#ifndef LOGGING_H
#define LOGGING_H

#include "SEGGER_RTT.h"
#include "globals.h"

#define LOGGING_RTT_OUTPUT_CHANNEL 1

extern uint8_t SEGGER_RTT_LOG_BUFFER[SEGGER_RTT_LOG_BUFFER_SIZE];

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
    #define fatal(msg, ...) fatal_impl(RTT_CTRL_TEXT_BRIGHT_RED "[FATAL|%s:%d]: " msg RTT_CTRL_RESET, __FILENAME__, __LINE__, ##__VA_ARGS__)
    #define warning(msg, ...)                                                                                                              \
        warning_impl(RTT_CTRL_TEXT_BRIGHT_RED "[WARNING|%s:%d]: " msg RTT_CTRL_RESET, __FILENAME__, __LINE__, ##__VA_ARGS__)
    #define event(msg, ...)                                                                                                                \
        event_impl(RTT_CTRL_TEXT_BRIGHT_WHITE "[EVENT|%s:%d]: " msg RTT_CTRL_RESET, __FILENAME__, __LINE__, ##__VA_ARGS__)
    #define info(msg, ...) info_impl(RTT_CTRL_TEXT_BRIGHT_WHITE "[INFO|%s:%d]: " msg RTT_CTRL_RESET, __FILENAME__, __LINE__, ##__VA_ARGS__)
    #ifdef UNITTEST
        #define test_log(msg, ...)                                                                                                         \
            debug_impl(RTT_CTRL_TEXT_WHITE "[TEST|%s:%d]: " msg RTT_CTRL_RESET, __FILENAME__, __LINE__, ##__VA_ARGS__)
        #define debug(msg, ...)
    #else
        #define test_log(msg, ...)
        #define debug(msg, ...) debug_impl(RTT_CTRL_TEXT_WHITE "[DEBUG|%s:%d]: " msg RTT_CTRL_RESET, __FILENAME__, __LINE__, ##__VA_ARGS__)
    #endif
#else
    /* Other build types (such as release or unittest) don't need filenames or line numbers */
    #define fatal(msg, ...)
    #define warning(msg, ...)
    #define event(msg, ...)
    #define info(msg, ...)
    #define debug(msg, ...)
    #define test_log(msg, ...)
#endif

void fatal_impl(const char *string, ...);
void warning_impl(const char *string, ...);
void event_impl(const char *string, ...);
void info_impl(char *string, ...);
void debug_impl(const char *string, ...);

void set_log_level(log_level_t level);
log_level_t get_log_level();

#define fatal_on_error(status, msg)                                                                                                        \
    do {                                                                                                                                   \
        if (status != SUCCESS) {                                                                                                           \
            fatal(msg);                                                                                                                    \
        }                                                                                                                                  \
    } while (0)
#define ret_err_status(status, msg)                                                                                                        \
    do {                                                                                                                                   \
        status_t s = status;                                                                                                               \
        if (s != SUCCESS) {                                                                                                                \
            warning(msg);                                                                                                                  \
            return s;                                                                                                                      \
        }                                                                                                                                  \
    } while (0)

#endif /* LOGGING_H */
