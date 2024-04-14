#ifndef TASK_MANAGER_TASK_H
#define TASK_MANAGER_TASK_H

#include "display_ssd1362.h"
#include "globals.h"
#include "rtos_start.h"
#include "watchdog_task.h"

#include <atmel_start.h>
#include <driver_init.h>

// Memory for the heartbeat task
#define TASK_MANAGER_TASK_STACK_SIZE 128 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
struct taskManagerTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t taskManagerTaskStack[TASK_MANAGER_TASK_STACK_SIZE];
    StaticTask_t taskManagerTaskTCB;
};

extern struct taskManagerTaskMemory taskManagerMem;

void task_manager_init(void *pvParameters);

#endif // HEARTBEAT_TASK_H