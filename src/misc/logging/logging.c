#include "logging.h"

#include "SEGGER_RTT.h"

#include <stdarg.h>
#include <stdlib.h>

// Macros for debugging functions so that file and line number info can be included

log_level_t LOG_LEVEL = DEFAULT_LOG_LEVEL;

void fatal_impl(const char *string, ...) {
    // No log level checking here, since fatal should always be printed
    va_list args;
    va_start(args, string);
    unsigned int BufferIndex = 0;
    SEGGER_RTT_vprintf(BufferIndex, string, &args); // Use vprintf to print with variable arguments
    // TODO: Gracefully shut down the system and then kick the watchdog.
    while (1) {} // Temporary infinite loop to halt the system (watchdog will eventually kick)

    va_end(args);
}

void warning_impl(const char *string, ...) {
    // Check if the log level is high enough to print this message
    if (WARNING < LOG_LEVEL) {
        return;
    }
    va_list args;
    va_start(args, string);
    unsigned int BufferIndex = 0;
    SEGGER_RTT_vprintf(BufferIndex, string, &args); // Use vprintf to print with variable arguments

    va_end(args);
}

void event_impl(const char *string, ...) {
    // Check if the log level is high enough to print this message
    if (EVENT < LOG_LEVEL) {
        return;
    }
    va_list args;
    va_start(args, string);
    unsigned int BufferIndex = 0;
    SEGGER_RTT_vprintf(BufferIndex, string, &args); // Use vprintf to print with variable arguments
    va_end(args);
}

void info_impl(char *string, ...) {
    // Check if the log level is high enough to print this message
    if (INFO < LOG_LEVEL) {
        return;
    }
    va_list args;
    va_start(args, string);
    unsigned int BufferIndex = 0;
    SEGGER_RTT_vprintf(BufferIndex, string, &args); // Use vprintf to print with variable arguments
    va_end(args);
}

void debug_impl(const char *string, ...) {
    // Check if the log level is high enough to print this message
    if (DEBUG < LOG_LEVEL) {
        return;
    }
    va_list args;
    va_start(args, string);
    unsigned int BufferIndex = 0;
    SEGGER_RTT_vprintf(BufferIndex, string, &args); // Use vprintf to print with variable arguments
    va_end(args);
}