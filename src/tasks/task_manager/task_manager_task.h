#ifndef TASK_MANAGER_TASK_H
#define TASK_MANAGER_TASK_H

// Includes
#include "globals.h"
#include "logging.h"
#include "rtos_start.h"
#include <atmel_start.h>
#include <driver_init.h>
#include "display_task.h"
#include "heartbeat_task.h"
#include "watchdog_task.h"
#include "shell_task.h"
#include "command_executor_task.h"

// Constants
#define TASK_MANAGER_TASK_STACK_SIZE           128            // Size of the stack in words (multiply by 4 to get bytes)
#define TASK_MANAGER_QUEUE_MAX_COMMANDS        15             // Maximum number of commands that can be queued at once for any task
#define TASK_MANAGER_QUEUE_ITEM_SIZE           sizeof(cmd_t)  // Size of each item in command queues
#define TASK_MANAGER_QUEUE_WAIT_MS             1000           // Wait time for sending/receiving a command to/from the queue (in ms)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
struct taskManagerTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t taskManagerTaskStack[TASK_MANAGER_TASK_STACK_SIZE];
    StaticQueue_t taskManagerTaskQueue;
    StaticTask_t taskManagerTaskTCB;
};

extern struct taskManagerTaskMemory taskManagerMem;

// A struct defining a task's lifecycle in the PVDXos RTOS
typedef struct {
    char *name;               // Name of the task
    bool enabled;             // Whether the task is enabled
    TaskHandle_t handle;      // FreeRTOS handle to the task
    TaskFunction_t function;  // Main entry point for the task
    uint32_t stackSize;       // Size of the stack in words (multiply by 4 to get bytes)
    StackType_t *stackBuffer; // Buffer for the stack
    void *pvParameters;       // Parameters to pass to the task's main function
    UBaseType_t priority;     // Priority of the task in the RTOS scheduler
    StaticTask_t *taskTCB;    // Task control block
    uint32_t watchdogTimeout; // How frequently the task should check in with the watchdog (in milliseconds)
    uint32_t lastCheckin;     // Last time the task checked in with the watchdog
    bool shouldCheckin;       // Whether the task is being monitored by the watchdog (initialized to NULL)
} PVDXTask_t;

// Global information about all tasks running on the system. This list is null-terminated.
extern PVDXTask_t taskList[];

// Exposed Functions
void task_manager_init(void);
void task_manager_init_subtasks(void);
void task_manager_main(void *pvParameters);
status_t toggle_task(int i)
PVDXTask_t task_manager_get_task(TaskHandle_t id);

#endif // TASK_MANAGER_TASK_H