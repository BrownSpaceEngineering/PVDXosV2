#ifndef DATASTORE_TASK_H
#define DATASTORE_TASK_H

#include "display_ssd1362.h"
#include "globals.h"
#include "rtos_start.h"
#include "watchdog_task.h"

#include <atmel_start.h>
#include <driver_init.h>

// Memory for the datastore
#define DATASTORE_TASK_STACK_SIZE 128 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
struct datastoreTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t taskManagerTaskStack[DATASTORE_TASK_STACK_SIZE];
    StaticTask_t taskManagerTaskTCB;
};

extern struct datastoreTaskMemory datastoreMem;

void datastore_init(void *pvParameters)

#endif // DATASTORE_TASK_H