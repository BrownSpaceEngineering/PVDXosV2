#ifndef DATASTORE_TASK_H
#define DATASTORE_TASK_H

// includes
#include <atmel_start.h>
#include <driver_init.h>
#include <stdint.h> // ?????
#include <task.h>

#include "globals.h"
#include "logging.h"
#include "ring_buffer.h"

// Memory for the datastore
#define DATASTORE_TASK_STACK_SIZE 1024        // Size of the stack in words (multiply by 4 to get bytes)
#define DATASTORE_QUEUE_WAIT_MS 1000          // Wait time for sending/receiving a command to/from the queue (in ms)
#define DATASTORE_DATA_BUFFER_TOTAL_SIZE 1024 // Size of the buffers.

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t datastore_task_stack[DATASTORE_TASK_STACK_SIZE];
    StaticQueue_t datastore_task_queue;
    StaticTask_t datastore_task_tcb;
} datastore_task_memory_t;

// Global memory for the datastore task
extern datastore_task_memory_t datastore_mem;
extern uint8_t datastore_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
extern QueueHandle_t datastore_command_queue_handle;

typedef struct {
    pvdx_task_t *task;
    ring_buffer_t rb;
} mapping_t;

#endif // DATASTORE_TASK_H