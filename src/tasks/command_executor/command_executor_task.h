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
#define COMMAND_QUEUE_MAX_COMMANDS 15             // Maximum number of commands that can be queued at once for any task
#define COMMAND_QUEUE_ITEM_SIZE sizeof(command_t) // Size of each item in command queues
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

// An enum to represent the various tasks/daemons that the command executor can interact with
typedef enum {
    TASK_COMMAND_EXECUTOR = 0,
    TASK_MANAGER,
    TASK_DISPLAY,
    TASK_SHELL,
    TASK_HEARTBEAT,
    TASK_MAGNETOMETER,
    TASK_CAMERA,
    TASK_9AXIS
} task_t;

// An enum to represent the different operations that the command executor can perform
// NOTE: The same operation can have different meanings depending on the target task
typedef enum {
    // General operations (can be overloaded by any task)
    OPERATION_SET_BUFFER = 0,
    OPERATION_UPDATE,
    OPERATION_POWER_OFF,
    OPERATION_SET_LOG_LEVEL,
    // Task-Manager specific operations
    OPERATION_INIT_SUBTASKS,
    OPERATION_ENABLE_SUBTASK,
    OPERATION_DISABLE_SUBTASK
} operation_t;

// A struct to represent a command that the command-executor can execute
typedef struct {
    task_t target;
    operation_t operation;
    char* p_data;
    size_t len;
    status_t* p_result;
    void (*callback)(status_t* p_result);
} command_t;

// Exposed functions
void command_executor_init(void);
void command_executor_main(void* pvParameters);
void command_executor_enqueue(command_t cmd);

#endif // COMMAND_EXECUTOR_H