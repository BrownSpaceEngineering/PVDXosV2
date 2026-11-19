/**
 * logging.c
 *
 * Logging functions for PVDXos. These functions allow for different log levels to be printed to the terminal
 * which can be filtered out based on the desired verbosity of the output.
 *
 * Created: February 25, 2024
 * Authors: Oren Kohavi, Guo Ma, Siddharta Laloux, Yi Liu
 */

#include "logging.h"

#include <stdarg.h>
#include <stdlib.h>

#include "SEGGER_RTT.h"
#include "os_delay.h"
#include "watchdog_task.h"

// Macros for debugging functions so that file and line number info can be included

log_level_t LOG_LEVEL = DEFAULT_LOG_LEVEL;
uint8_t SEGGER_RTT_LOG_BUFFER[SEGGER_RTT_LOG_BUFFER_SIZE];

// Messages that didn't fit in the RTT buffer since the last "log lines dropped" notice
static uint32_t dropped_lines = 0;

/**
 * Prints to the log channel. The channel skips whatever doesn't fit (SEGGER_RTT_MODE_NO_BLOCK_SKIP), so a message can
 * be lost whole or cut short; each one is counted, and the next message that fits is preceded by a notice of how many
 * were lost (on a new line, in case one was cut short). The flatsat test rig fails a test on that notice.
 */
static void log_vprintf(const char *string, va_list *args) {
    uint32_t dropped = __atomic_exchange_n(&dropped_lines, 0, __ATOMIC_RELAXED);
    if (dropped != 0 && SEGGER_RTT_printf(LOGGING_RTT_OUTPUT_CHANNEL, "\n[!] %lu log lines dropped\n", (unsigned long)dropped) < 0) {
        __atomic_fetch_add(&dropped_lines, dropped, __ATOMIC_RELAXED);
    }
    if (SEGGER_RTT_vprintf(LOGGING_RTT_OUTPUT_CHANNEL, string, args) < 0) {
        __atomic_fetch_add(&dropped_lines, 1, __ATOMIC_RELAXED);
    }
}

void fatal_impl(const char *string, ...) {
    // No log level checking here, since fatal should always be printed
    va_list args;
    va_start(args, string);
    log_vprintf(string, &args);

    // To make sure that the message is printed before the watchdog resets the system, print a few more.
    warning_impl("FATAL ERROR OCCURRED! RESTARTING SYSTEM...\n");
    warning_impl("FATAL ERROR OCCURRED! RESTARTING SYSTEM...\n");
    warning_impl("FATAL ERROR OCCURRED! RESTARTING SYSTEM...\n");
    os_delay_ms(1000); // Wait for the message to be printed

    // Force reboot
    kick_watchdog();

    // This line should never be reached, but we include it to adhere to the va_list contract
    va_end(args);
}

void fatal_no_log_impl(void) {
    // Force reboot
    kick_watchdog();
}

void warning_impl(const char *string, ...) {
    // No log level checking here, since warning should always be printed
    va_list args;
    va_start(args, string);
    log_vprintf(string, &args);

    va_end(args);
}

void event_impl(const char *string, ...) {
    // Check if the log level is high enough to print this message
    if (EVENT < LOG_LEVEL) {
        return;
    }
    va_list args;
    va_start(args, string);
    log_vprintf(string, &args);
    va_end(args);
}

void info_impl(char *string, ...) {
    // Check if the log level is high enough to print this message
    if (INFO < LOG_LEVEL) {
        return;
    }
    va_list args;
    va_start(args, string);
    log_vprintf(string, &args);
    va_end(args);
}

void debug_impl(const char *string, ...) {
    // Check if the log level is high enough to print this message
    if (DEBUG < LOG_LEVEL) {
        return;
    }
    va_list args;
    va_start(args, string);
    log_vprintf(string, &args);
    va_end(args);
}

void set_log_level(log_level_t level) {
    LOG_LEVEL = level;
}

log_level_t get_log_level() {
    return LOG_LEVEL;
}
