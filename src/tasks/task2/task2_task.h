#ifndef TASK2_H
#define TASK2_H

// Includes
#include "globals.h"
#include "logging.h"
#include "queue.h"
#include "task_list.h"

// Constants
#define TASK2_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t task2_task_stack[TASK2_TASK_STACK_SIZE];
    uint8_t task2_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t task2_task_queue;
    StaticTask_t task2_task_tcb;
} task2_task_memory_t;

extern task2_task_memory_t task2_mem;

QueueHandle_t init_task2(void);
void main_task2(void *pvParameters);
void exec_command_task2(command_t *const p_cmd);
#endif // TASK2_H