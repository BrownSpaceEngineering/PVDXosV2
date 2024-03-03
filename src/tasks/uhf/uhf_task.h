#ifndef UHF_TASK_H
#define UHF_TASK_H

#include "globals.h"
#include "logging.h"
#include "rtos_start.h"
#include "watchdog_task.h"

#include <atmel_start.h>
#include <driver_init.h>

// Memory for the heartbeat task
#define UHF_TASK_STACK_SIZE 512 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
struct uhfTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t uhfTaskStack[UHF_TASK_STACK_SIZE];
    StaticTask_t uhfTaskTCB;
};

extern struct uhfTaskMemory uhfMem;

void uhf_main(void *pvParameters);

#endif // UHF_TASK_H