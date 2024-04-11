#ifndef SHELL_TASK_H
#define SHELL_TASK_H

#include "globals.h"
#include "rtos_start.h"
#include "watchdog_task.h"

#include <atmel_start.h>
#include <driver_init.h>

// Memory for the shell task
#define SHELL_TASK_STACK_SIZE 128 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
struct shellTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t shellTaskStack[SHELL_TASK_STACK_SIZE];
    StaticTask_t shellTaskTCB;
};

extern struct shellTaskMemory shellMem;

void shell_main(void *pvParameters);

#endif // SHELL_TASK_H