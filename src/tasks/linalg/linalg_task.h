#ifndef LINALG_H
#define LINALG_H

// Includes
#include "globals.h"
#include "logging.h"
#include "queue.h"
#include "task_list.h"

// Constants
#define LINALG_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t linalg_task_stack[LINALG_TASK_STACK_SIZE];
    uint8_t linalg_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t linalg_task_queue;
    StaticTask_t linalg_task_tcb;
} linalg_task_memory_t;

extern linalg_task_memory_t linalg_mem;

QueueHandle_t init_linalg(void);
void main_linalg(void *pvParameters);
void exec_command_linalg(command_t *const p_cmd);

#endif // LINALG_H