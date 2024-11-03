#ifndef COMMAND_DISPATCHER_H
#define COMMAND_DISPATCHER_H

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
#define COMMAND_DISPATCHER_TASK_STACK_SIZE 128      // Size of the stack in words (multiply by 4 to get bytes)
#define COMMAND_QUEUE_WAIT_MS 1000                // Wait time for sending/receiving a command to/from the queue (in ms)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t command_dispatcher_task_stack[COMMAND_DISPATCHER_TASK_STACK_SIZE];
    StaticQueue_t command_dispatcher_task_queue;
    StaticTask_t command_dispatcher_task_tcb;
} command_dispatcher_task_memory_t;

extern command_dispatcher_task_memory_t command_dispatcher_mem;

// Queue for commands to be executed by the command dispatcher
extern uint8_t command_dispatcher_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
extern QueueHandle_t command_dispatcher_command_queue;

// Exposed functions
void init_command_dispatcher(void);
void main_command_dispatcher(void* pvParameters);
void exec_command_command_dispatcher(command_t cmd);
// Exposed functions that go through the command dispatcher
void command_dispatcher_enqueue(command_t cmd);

#endif // COMMAND_DISPATCHER_H