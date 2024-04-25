#ifndef COMMAND_EXECUTOR_H
#define COMMAND_EXECUTOR_H

#include "globals.h"
#include "FreeRTOS.h"
#include "stdbool.h"
#include "queue.h"
#include "task.h"
#include "task_manager_task.h"
#include "logging.h"

#define COMMAND_EXECUTOR_TASK_STACK_SIZE  128            // Size of the stack in words (multiply by 4 to get bytes)
#define MAX_COMMANDS                      50             // Maximum number of commands that can be queued at once
#define QUEUE_ITEM_SIZE                   sizeof(void*)  // Size of each item in the queue
#define COMMAND_QUEUE_WAIT_MS             1000           // Wait time for receiving a command from the queue (in ms)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
struct commandExecutorTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t commandExecutorTaskStack[COMMAND_EXECUTOR_TASK_STACK_SIZE];
    StaticTask_t commandExecutorTaskTCB;
    StaticQueue_t commandQueue;
};

extern struct commandExecutorTaskMemory commandExecutorMem;

// Queue for commands to be executed by the command executor
extern QueueHandle_t commandQueue;

// An enum to represent the different operations that the command executor can perform
typedef enum {
    OPERATION_DISPLAY_SET_BUFFER = 0,
    OPERATION_DISPLAY_UPDATE,
    OPERATION_DISPLAY_OFF, 
    OPERATION_LOG_SET_LEVEL,
} operation_t;

// A struct to represent a command that the command-executor can execute
typedef struct {
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