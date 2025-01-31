/**
 * logging.c
 * 
 * Logging functions for PVDXos. These functions allow for different log levels to be printed to the terminal
 * which can be filtered out based on the desired verbosity of the output.
 * 
 * Created: February 25, 2024
 * Authors: Oren Kohavi, Guo Ma
 */

#include "logging.h"

#include <stdarg.h>
#include <stdlib.h>

#include "SEGGER_RTT.h"
#include "watchdog_task.h"

// Macros for debugging functions so that file and line number info can be included

log_level_t LOG_LEVEL = DEFAULT_LOG_LEVEL;
uint8_t SEGGER_RTT_LOG_BUFFER[SEGGER_RTT_LOG_BUFFER_SIZE];

void fatal_impl(const char *string, ...) {
    // No log level checking here, since fatal should always be printed
    va_list args;
    va_start(args, string);
    SEGGER_RTT_vprintf(LOGGING_RTT_OUTPUT_CHANNEL, string, &args); // Use vprintf to print with variable arguments

    // To make sure that the message is printed before the watchdog resets the system, print a few more.
    warning_impl("FATAL ERROR OCCURRED! RESTARTING SYSTEM...\n");
    warning_impl("FATAL ERROR OCCURRED! RESTARTING SYSTEM...\n");
    warning_impl("FATAL ERROR OCCURRED! RESTARTING SYSTEM...\n");
    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for the message to be printed

    // TODO: Gracefully shut down the system before kicking the watchdog
    kick_watchdog();

    // This line should never be reached, but we include it to adhere to the va_list contract
    va_end(args);
}

void warning_impl(const char *string, ...) {
    // No log level checking here, since warning should always be printed
    va_list args;
    va_start(args, string);
    SEGGER_RTT_vprintf(LOGGING_RTT_OUTPUT_CHANNEL, string, &args); // Use vprintf to print with variable arguments

    va_end(args);
}

void event_impl(const char *string, ...) {
    // Check if the log level is high enough to print this message
    if (EVENT < LOG_LEVEL) {
        return;
    }
    va_list args;
    va_start(args, string);
    SEGGER_RTT_vprintf(LOGGING_RTT_OUTPUT_CHANNEL, string, &args); // Use vprintf to print with variable arguments
    va_end(args);
}

void info_impl(char *string, ...) {
    // Check if the log level is high enough to print this message
    if (INFO < LOG_LEVEL) {
        return;
    }
    va_list args;
    va_start(args, string);
    SEGGER_RTT_vprintf(LOGGING_RTT_OUTPUT_CHANNEL, string, &args); // Use vprintf to print with variable arguments
    va_end(args);
}

void debug_impl(const char *string, ...) {
    // Check if the log level is high enough to print this message
    if (DEBUG < LOG_LEVEL) {
        return;
    }
    va_list args;
    va_start(args, string);
    SEGGER_RTT_vprintf(LOGGING_RTT_OUTPUT_CHANNEL, string, &args); // Use vprintf to print with variable arguments
    va_end(args);
}

void set_log_level(log_level_t level) {
    LOG_LEVEL = level;
}

log_level_t get_log_level() {
    return LOG_LEVEL;
}