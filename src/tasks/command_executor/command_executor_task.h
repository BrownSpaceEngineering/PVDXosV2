#ifndef COMMAND_EXECUTOR_H
#define COMMAND_EXECUTOR_H

#include "globals.h"

#define COMMAND_EXECUTOR_TASK_STACK_SIZE 128 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
struct commandExecutorTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t commandExecutorTaskStack[COMMAND_EXECUTOR_TASK_STACK_SIZE];
    StaticTask_t commandExecutorTaskTCB;
};

extern struct commandExecutorTaskMemory commandExecutorMem;

// An enum to represent the different operations that the command executor can perform
typedef enum {
    OPERATION_DISPLAY_SET_BUFFER = 0,
    OPERATION_DISPLAY_UPDATE,
    OPERATION_DISPLAY_OFF, 
    OPERATION_LOG_SET_LEVEL,
    OPERATION_MAGNETOMETER_READ,
    OPERATION_CAMERA_CAPTURE,
} operation_t;

// A struct to represent a command that the command-executor can execute
typedef struct {
    operation_t operation;
    char* p_data;
    size_t len;
    status_t* p_result;
    (*callback)(status_t);
};

// Exposed functions
void command_executor_main(void* pvParameters);

#endif // COMMAND_EXECUTOR_H