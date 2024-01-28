#pragma once
#define NUM_TASKS 2

typedef enum {
    WATCHDOG_TASK = 0,
    HEARTBEAT_TASK = 1,
} task_type_t;