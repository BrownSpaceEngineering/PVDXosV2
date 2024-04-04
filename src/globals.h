#ifndef GLOBALS_H
#define GLOBALS_H

/* --------- HIGH-LEVEL CONFIG VARIABLES AND DEFINES --------- */

#if defined(RELEASE)
#define DEFAULT_LOG_LEVEL EVENT // The default log level for the system
#else
#define DEFAULT_LOG_LEVEL INFO // The default log level for the system
#endif

/* ----------------------------------------------------------- */

/*
 * Nothing here yet, but stuff that goes here would be things like how long to wait until antenna deploy, flags for turning on/off certain
 * features, etc.
 * */

// ### IN-DEPTH CONFIG VARIABLES AND DEFINITIONS ###

#define TASK_STACK_OVERFLOW_PADDING 8 // Buffer for the stack size so that overflow doesn't corrupt any TCBs

#define NUM_TASKS 2 // The number of tasks that the watchdog will check in with

typedef enum {
    WATCHDOG_TASK = 0,
    HEARTBEAT_TASK = 1,
} task_type_t;

typedef enum {
    SUCCESS = 0,

    // Recoverable or low-significance errors are all less than 64
    ERROR_INTERNAL, // Generic error for when something goes wrong
    ERROR_NO_DATA,
    ERROR_NO_MEMORY,
    ERROR_WRITE_FAILED,
    ERROR_NOT_YET_IMPLEMENTED,
    ERROR_RESOURCE_IN_USE, // Similar to EAGAIN in Linux (Basically, this WOULD work but busy rn, try again later)
    ERROR_MAX_SIZE_EXCEEDED,
    ERROR_UNINITIALIZED,

    // High significance errors start at 128 (0x80) (in these cases, restart the system)
    ERROR_UNRECOVERABLE = 0x80,
    ERROR_BITFLIP, // Specifically if we detect a bitflip, so we can increment counters.
} status_t;

typedef enum {
    DEBUG = 0,
    INFO,
    EVENT,
    WARNING,
    FATAL,
} log_level_t;

#endif // GLOBALS_H