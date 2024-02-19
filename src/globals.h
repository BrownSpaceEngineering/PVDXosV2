#ifndef GLOBALS_H
#define GLOBALS_H

#define NUM_TASKS 2 // The number of tasks that the watchdog will check in with

typedef enum {
    WATCHDOG_TASK = 0,
    HEARTBEAT_TASK = 1,
} task_type_t;

#endif // GLOBALS_H