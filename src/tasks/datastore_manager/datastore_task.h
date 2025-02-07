#ifndef DATASTORE_TASK_H
#define DATASTORE_TASK_H

#include <atmel_start.h>
#include <driver_init.h>

#include "globals.h"

// Memory for the datastore
#define DATASTORE_TASK_STACK_SIZE 128 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t datastore_task_stack[TASK_MANAGER_TASK_STACK_SIZE];
    StaticQueue_t datastore_task_queue;
    StaticTask_t datastore_task_tcb;
} datastore_task_memory_t;

// Global memory for the datastore task
extern datastore_task_memory_t datastore_mem;
extern uint8_t datastore_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
extern QueueHandle_t datastore_command_queue_handle;

#endif // DATASTORE_TASK_H