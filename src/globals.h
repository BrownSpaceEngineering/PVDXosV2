#ifndef GLOBALS_H
#define GLOBALS_H

#define TASK_STACK_OVERFLOW_PADDING 16 // Buffer for the stack size so that overflow doesn't corrupt any TCBs

#define NUM_TASKS 2 // The number of tasks that the watchdog will check in with

typedef enum {
    WATCHDOG_TASK = 0,
    HEARTBEAT_TASK = 1,
} task_type_t;

typedef enum {
    SUCCESS = 0,
    ERROR_INTERNAL, // Generic error for when something goes wrong
    ERROR_NO_DATA,
    ERROR_NO_MEMORY,
    ERROR_WRITE_FAILED,
    ERROR_NOT_YET_IMPLEMENTED,
    ERROR_BUSY, // Similar to EAGAIN in Linux, if that comparison is helpful

    // This one should be last in the list
    ERROR_UNRECOVERABLE, // If this is returned, the system should restart.
} status_t;

#endif // GLOBALS_H