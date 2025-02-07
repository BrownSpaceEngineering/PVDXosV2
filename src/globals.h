#ifndef GLOBALS_H
#define GLOBALS_H

/* --------- HIGH-LEVEL CONFIG VARIABLES AND DEFINES --------- */

#if defined(RELEASE)
    #define DEFAULT_LOG_LEVEL INFO // The default log level for the system on release builds
#else
    #define DEFAULT_LOG_LEVEL INFO // The default log level for the system for debug and unit test builds
#endif

#define SEGGER_RTT_LOG_BUFFER_SIZE 2048 // How big the RTT buffer is for logging (this buffer is flushed to the host regularly)

/* ----------------------------------------------------------- */

#define TASK_STACK_OVERFLOW_PADDING 16 // Buffer for the stack size so that overflow doesn't corrupt any TCBs

#define NUM_TASKS 5 // The number of tasks that the watchdog will check in with

typedef enum {
    WATCHDOG_TASK = 0,
    TASK_MANAGER_TASK = 1,
    HEARTBEAT_TASK = 2,
    DISPLAY_TASK = 3,
    SHELL_TASK = 4,
    DATASTORE_TASK = 5,
} task_type_t;

typedef enum {
    SUCCESS = 0,

    // Recoverable or low-significance errors are all less than 128
    ERROR_INTERNAL, // Generic error for when something goes wrong
    ERROR_NO_DATA,
    ERROR_NO_MEMORY,
    ERROR_WRITE_FAILED,
    ERROR_NOT_YET_IMPLEMENTED,
    ERROR_RESOURCE_IN_USE, // Similar to EAGAIN in Linux (Basically, this WOULD work but busy rn, try again later)
    ERROR_MAX_SIZE_EXCEEDED,
    ERROR_UNINITIALIZED,
    ERROR_IO,

    // High significance errors start at 128 (0x80) (in these cases, restart the system)
    ERROR_UNRECOVERABLE = 0x80,
    ERROR_BITFLIP, // Specifically if we detect a bitflip, so we can increment counters.
} status_t;

typedef enum {
    DEBUG = 0,
    INFO,
    EVENT,
    WARNING,
} log_level_t;

// Defines for printing out the build version
#if defined(DEVBUILD)
    #define BUILD_TYPE "Development Build"
#endif
#if defined(UNITTEST)
    #define BUILD_TYPE "Unit Test Build"
#endif
#if defined(RELEASE)
    #define BUILD_TYPE "Release Build"
#endif

// Define build date
#define BUILD_DATE __DATE__

// Define build timame
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