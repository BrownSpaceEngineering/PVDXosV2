#ifndef TASK_MANAGER_TASK_H
#define TASK_MANAGER_TASK_H

#include "display_task.h"
#include "globals.h"
#include "heartbeat_task.h"
#include "logging.h"
#include "rtos_start.h"
#include "watchdog_task.h"

#include <atmel_start.h>
#include <driver_init.h>

// Memory for the heartbeat task
#define TASK_MANAGER_TASK_STACK_SIZE 128 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
struct taskManagerTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t taskManagerTaskStack[TASK_MANAGER_TASK_STACK_SIZE];
    StaticTask_t taskManagerTaskTCB;
};

extern struct taskManagerTaskMemory taskManagerMem;

typedef struct {
    TaskHandle_t handle;      // Handle to the task (used to communicate with the task)
    TaskFunction_t function;  // Main entry point for the task
    char *name;               // Name of the task
    uint32_t stackSize;       // Size of the stack in words (multiply by 4 to get bytes)
    StackType_t *stackBuffer; // Buffer for the stack
    void *pvParameters;       // Parameters to pass to the task's main function
    UBaseType_t priority;     // Priority of the task in the RTOS scheduler
    StaticTask_t *taskTCB;    // Task control block
    uint32_t watchdogTimeout; // How frequently the task should check in with the watchdog (in milliseconds)
    uint32_t lastCheckin;     // Last time the task checked in with the watchdog
    bool shouldCheckin;       // Whether the task is being monitored by the watchdog
} PVDXTask_t;

// Global information about all tasks running on the system
extern PVDXTask_t taskList[];

// Exposed Functions
void task_manager_init(void);
void task_manager_main(void *pvParameters);

#endif // TASK_MANAGER_TASK_H