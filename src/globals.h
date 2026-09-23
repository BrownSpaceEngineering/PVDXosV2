/**
 * globals.h
 *
 * Defines global datatypes, structures and headers.
 *
 * Trimmed down for the bare-metal camera debug build: all FreeRTOS/task
 * definitions have been removed since this build runs no scheduler.
 *
 * Created:
 * Authors: Siddharta Laloux
 */

#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------- LOGGING CONSTANTS ---------- */

#if defined(RELEASE)
    #define DEFAULT_LOG_LEVEL INFO // The default log level for the system on release builds
#else
    #define DEFAULT_LOG_LEVEL DEBUG // The default log level for the system for debug and unit test builds
#endif

/* ---------- ENUMS ---------- */

// An enum to represent the different statuses that a function can return
typedef enum {
    // Standard Responses
    PROCESSING = 0,   // Function is still processing and will return a result later
    NO_STATUS_RETURN, // Function does not return a status, so don't check it
    SUCCESS,

    // Error Responses
    ERROR_READ_FAILED,
    ERROR_WRITE_FAILED,
    ERROR_SPI_TRANSFER_FAILED,
    ERROR_I2C_FAILED,
    ERROR_TASK_DISABLED,
    ERROR_BAD_TARGET,
    ERROR_SANITY_CHECK_FAILED,
    ERROR_NOT_READY,
} status_t;

// An enum to represent the different log levels that functions can use
typedef enum {
    DEBUG = 0,
    INFO,
    EVENT,
    WARNING,
} log_level_t;

/* ---------- BUILD CONSTANTS ---------- */

// Defines for printing out the build version
#if defined(UNITTEST)
    #define BUILD_TYPE "Unit Test Build"
#elif defined(DEVBUILD)
    #define BUILD_TYPE "Development Build"
#elif defined(RELEASE)
    #define BUILD_TYPE "Release Build"
#endif

// Define build date
#define BUILD_DATE __DATE__

// Define build time
#define BUILD_TIME __TIME__

// IDE-only defines so that the IDE doesn't throw a billion errors for unavailable defines
#if !defined(DEVBUILD) && !defined(UNITTEST) && !defined(RELEASE) // No build flags set so it must be IDE
    #define BUILD_TYPE "<Resolved During Compilation>"
    #define GIT_BRANCH_NAME "<Resolved During Compilation>"
    #define GIT_COMMIT_HASH "<Resolved During Compilation>"
    #define DEVBUILD
    #error "IDE-only defines ran during a build! This should never happen!"
#endif

#endif // GLOBALS_H
