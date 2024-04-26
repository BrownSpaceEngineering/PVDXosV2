#ifndef COMMAND_EXECUTOR_H
#define COMMAND_EXECUTOR_H

// Includes
#include "globals.h"
#include "FreeRTOS.h"
#include "stdbool.h"
#include "queue.h"
#include "task.h"
#include "task_manager_task.h"
#include "logging.h"
#include "display_task.h"

// Constants
#define COMMAND_EXECUTOR_TASK_STACK_SIZE  128            // Size of the stack in words (multiply by 4 to get bytes)
#define MAX_COMMANDS                      15             // Maximum number of commands that can be queued at once
#define QUEUE_ITEM_SIZE                   sizeof(void*)  // Size of each item in the queue
#define COMMAND_QUEUE_WAIT_MS             1000           // Wait time for receiving a command from the queue (in ms)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
struct commandExecutorTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t commandExecutorTaskStack[COMMAND_EXECUTOR_TASK_STACK_SIZE];
    StaticTask_t commandExecutorTaskTCB;
    StaticQueue_t commandExecutorTaskQueue;
};

extern struct commandExecutorTaskMemory commandExecutorMem;

// Queue for commands to be executed by the command executor
extern QueueHandle_t commandQueue;

// An enum to represent the varous tasks/daemons that the command executor can interact with
typedef enum {
    TASK_COMMAND_EXECUTOR = 0,
    TASK_DISPLAY,
} task_t;

// An enum to represent the different operations that the command executor can perform
// NOTE: The same operation can have different meanings depending on the target task
typedef enum {
    OPERATION_SET_BUFFER = 0,
    OPERATION_UPDATE,
    OPERATION_POWER_OFF, 
    OPERATION_SET_LEVEL,
} operation_t;

// A struct to represent a command that the command-executor can execute
typedef struct {
    task_t target;
    operation_t operation;
    char* p_data;
    size_t len;
    status_t* p_result;
    void (*callback)(status_t* p_result);
} cmd_t;

// Exposed functions
void command_executor_init(void);
void command_executor_main(void* pvParameters);
void command_executor_enqueue(cmd_t* p_cmd);

#endif // COMMAND_EXECUTOR_H