#ifndef HEARTBEAT_TASK_H
#define HEARTBEAT_TASK_H

// Includes
#include <atmel_start.h>
#include <driver_init.h>
#include "globals.h"
#include "rtos_start.h"
#include "watchdog_task.h"

//Memory for the heartbeat task
#define HEARTBEAT_TASK_STACK_SIZE 128 // Size of the stack in words (multiply by 4 to get bytes)

//Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t heartbeat_task_stack[HEARTBEAT_TASK_STACK_SIZE];
    StaticTask_t heartbeat_task_tcb;
} heartbeat_task_memory_t;

extern heartbeat_task_memory_t heartbeat_mem;

void heartbeat_main(void *pvParameters);

#endif // HEARTBEAT_TASK_H