#ifndef TASK_MANAGER_TASK_H
#define TASK_MANAGER_TASK_H

// Includes
#include <atmel_start.h>
#include <driver_init.h>

#include "command_executor_task.h"
#include "display_task.h"
#include "globals.h"
#include "heartbeat_task.h"
#include "logging.h"
#include "rtos_start.h"
#include "shell_task.h"
#include "utils.h"
#include "watchdog_task.h"

// Constants
#define TASK_MANAGER_TASK_STACK_SIZE 128           // Size of the stack in words (multiply by 4 to get bytes)
#define TASK_MANAGER_QUEUE_MAX_COMMANDS 15         // Maximum number of commands that can be queued at once for any task
#define TASK_MANAGER_QUEUE_ITEM_SIZE sizeof(Command) // Size of each item in command queues
#define TASK_MANAGER_QUEUE_WAIT_MS 1000            // Wait time for sending/receiving a command to/from the queue (in ms)

// Represents the end of a PVDXTask array, contains all null parameters
#define NULL_TASK ((PVDXTask){NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL})

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
struct taskManagerTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t taskManagerTaskStack[TASK_MANAGER_TASK_STACK_SIZE];
    StaticQueue_t taskManagerTaskQueue;
    StaticTask_t taskManagerTaskTCB;
};

// Global memory for the task manager task
extern struct taskManagerTaskMemory task_manager_mem;
extern uint8_t task_manager_queue_buffer[TASK_MANAGER_QUEUE_MAX_COMMANDS * TASK_MANAGER_QUEUE_ITEM_SIZE];
extern QueueHandle_t task_manager_cmd_queue;

// Mutex related variables
extern SemaphoreHandle_t task_list_mutex;
extern StaticSemaphore_t task_list_mutex_buffer;

// A struct defining a task's lifecycle in the PVDXos RTOS
typedef struct {
    char *name;               // Name of the task
    bool enabled;             // Whether the task is enabled
    TaskHandle_t handle;      // FreeRTOS handle to the task
    TaskFunction_t function;  // Main entry point for the task
    uint32_t stack_size;       // Size of the stack in words (multiply by 4 to get bytes)
    StackType_t *stack_buffer; // Buffer for the stack
    void *pvParameters;       // Parameters to pass to the task's main function
    UBaseType_t priority;     // Priority of the task in the RTOS scheduler
    StaticTask_t *task_tcb;    // Task control block
    uint32_t watchdog_timeout; // How frequently the task should check in with the watchdog (in milliseconds)
    uint32_t last_checkin;     // Last time the task checked in with the watchdog
    bool has_registered;      // Whether the task is being monitored by the watchdog (initialized to NULL)
} PVDXTask;

// Global information about all tasks running on the system. This list is null-terminated.
extern PVDXTask task_list[];

// Exposed Functions
PVDXTask *get_task(TaskHandle_t id);
void init_task(size_t i);
void task_manager_init(void);
void task_manager_init_subtasks(void);
void task_manager_main(void *pvParameters);

#endif // TASK_MANAGER_TASK_H