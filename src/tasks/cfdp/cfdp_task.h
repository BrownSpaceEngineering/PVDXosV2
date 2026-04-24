#ifndef CFDP_TASK_H
#define CFDP_TASK_H

// Includes
#include <atmel_start.h>
#include <driver_init.h>

#include "globals.h"
#include "tasks/cfdp/cfdp_engine.h"
#include "tasks/watchdog/watchdog_task.h"

// Memory for the CFDP task
#define CFDP_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

#define CFDP_MAX_ACTIVE_TRANSACTIONS 4

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t cfdp_task_stack[CFDP_TASK_STACK_SIZE];
    uint8_t cfdp_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    cfdp_transaction_t cfdp_transactions[CFDP_MAX_ACTIVE_TRANSACTIONS];
    StaticQueue_t cfdp_task_queue;
    StaticTask_t cfdp_task_tcb;
} cfdp_task_memory_t;

extern cfdp_task_memory_t cfdp_mem;

QueueHandle_t init_cfdp(void);
void main_cfdp(void *pvParameters);

#endif // CFDP_TASK_H
