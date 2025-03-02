#ifndef TEST_ONE_H
#define TEST_ONE_H

// Includes
#include "globals.h"
#include "logging.h"
#include "queue.h"
#include "task_list.h"

// Constants
#define TEST_ONE_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t test_one_task_stack[TEST_ONE_TASK_STACK_SIZE];
    uint8_t test_one_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t test_one_task_queue;
    StaticTask_t test_one_task_tcb;
} test_one_task_memory_t;

extern test_one_task_memory_t test_one_mem;

QueueHandle_t init_test_one(void);
void handle_cmd_test_one(command_t *p_cmd);
void main_test_one(void *pvParameters);

#endif // TEST_ONE_H