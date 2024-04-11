#ifndef GLOBALS_H
#define GLOBALS_H

/* --------- HIGH-LEVEL CONFIG VARIABLES AND DEFINES --------- */

#if defined(RELEASE)
    #define DEFAULT_LOG_LEVEL INFO // The default log level for the system on release builds
#else
    #define DEFAULT_LOG_LEVEL DEBUG // The default log level for the system for debug and unit test builds
#endif

/* ----------------------------------------------------------- */

#define TASK_STACK_OVERFLOW_PADDING 16 // Buffer for the stack size so that overflow doesn't corrupt any TCBs
#define NUM_TASKS                   4 // The number of tasks that the watchdog will check in with
#define size_t uint32_t;


typedef enum {
    WATCHDOG_TASK = 0,
    TASK_MANAGER_TASK = 1,
    HEARTBEAT_TASK = 2,
    DISPLAY_TASK = 3,
} task_type_t;

typedef enum {
    SUCCESS = 0,
    ERROR_NO_DATA,
    ERROR_NO_MEMORY,
    ERROR_UNRECOVERABLE, // If this is returned, the system should restart.
    ERROR_INTERNAL,
    ERROR_IO,
} status_t;

typedef enum {
    DEBUG = 0,
    INFO,
    EVENT,
    WARNING,
    FATAL,
} log_level_t;

#endif // GLOBALS_H