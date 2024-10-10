#ifndef COMMAND_EXECUTOR_H
#define COMMAND_EXECUTOR_H

// Includes
#include "FreeRTOS.h"
#include "display_task.h"
#include "globals.h"
#include "logging.h"
#include "queue.h"
#include "stdbool.h"
#include "task.h"
#include "task_manager_task.h"

// Constants
#define COMMAND_EXECUTOR_TASK_STACK_SIZE 128      // Size of the stack in words (multiply by 4 to get bytes)
#define COMMAND_QUEUE_WAIT_MS 1000                // Wait time for sending/receiving a command to/from the queue (in ms)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t command_executor_task_stack[COMMAND_EXECUTOR_TASK_STACK_SIZE];
    StaticQueue_t command_executor_task_queue;
    StaticTask_t command_executor_task_tcb;
} command_executor_task_memory_t;

extern command_executor_task_memory_t command_executor_mem;

// Queue for commands to be executed by the command executor
extern QueueHandle_t command_executor_cmd_queue;

// Exposed functions
void command_executor_init(void);
void command_executor_main(void* pvParameters);
void command_executor_enqueue(command_t cmd);

#endif // COMMAND_EXECUTOR_H