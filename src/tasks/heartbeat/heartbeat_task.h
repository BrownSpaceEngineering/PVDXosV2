#ifndef HEARTBEAT_TASK_H
#define HEARTBEAT_TASK_H

#include <atmel_start.h>
#include <driver_init.h>
#include "globals.h"
#include "rtos_start.h"
#include "watchdog_task.h"

//Memory for the heartbeat task
#define HEARTBEAT_TASK_STACK_SIZE 128 // Size of the stack in words (multiply by 4 to get bytes)

//Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
struct heartbeatTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t heartbeatTaskStack[HEARTBEAT_TASK_STACK_SIZE];
    StaticTask_t heartbeatTaskTCB;
};

extern struct heartbeatTaskMemory heartbeatMem;

void heartbeat_main(void *pvParameters);

#endif // HEARTBEAT_TASK_H