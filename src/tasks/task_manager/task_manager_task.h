#ifndef TASK_MANAGER_TASK_H
#define TASK_MANAGER_TASK_H

// Includes
#include <atmel_start.h>
#include <driver_init.h>

#include "command_dispatcher_task.h"
#include "display_task.h"
#include "globals.h"
#include "heartbeat_task.h"
#include "logging.h"
#include "rtos_start.h"
#include "shell_task.h"
#include "utils.h"
#include "watchdog_task.h"

// Constants
#define TASK_MANAGER_TASK_STACK_SIZE 128               // Size of the stack in words (multiply by 4 to get bytes)
#define TASK_MANAGER_QUEUE_WAIT_MS 1000                // Wait time for sending/receiving a command to/from the queue (in ms)

// Represents the end of a pvdx_task_t array, contains all null parameters
#define NULL_TASK ((pvdx_task_t){NULL, false, NULL, NULL, 0, NULL, NULL, 0, NULL, 0, 0, false})

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t task_manager_task_stack[TASK_MANAGER_TASK_STACK_SIZE];
    StaticQueue_t task_manager_task_queue;
    StaticTask_t task_manager_task_tcb;
} task_manager_task_memory_t;

// Global memory for the task manager task
extern task_manager_task_memory_t task_manager_mem;
extern uint8_t task_manager_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
extern QueueHandle_t task_manager_cmd_queue;

// Mutex related variables
extern SemaphoreHandle_t task_list_mutex;
extern StaticSemaphore_t task_list_mutex_buffer;

// A struct defining a task's lifecycle in the PVDXos RTOS
typedef struct {
    char* name;               // Name of the task
    bool enabled;             // Whether the task is enabled
    TaskHandle_t handle;      // FreeRTOS handle to the task
    TaskFunction_t function;  // Main entry point for the task
    uint32_t stack_size;       // Size of the stack in words (multiply by 4 to get bytes)
    StackType_t* stack_buffer; // Buffer for the stack
    void* pvParameters;       // Parameters to pass to the task's main function
    UBaseType_t priority;     // Priority of the task in the RTOS scheduler
    StaticTask_t* task_tcb;    // Task control block
    uint32_t watchdog_timeout; // How frequently the task should check in with the watchdog (in milliseconds)
    uint32_t last_checkin;     // Last time the task checked in with the watchdog
    bool has_registered;      // Whether the task is being monitored by the watchdog (initialized to NULL)
} pvdx_task_t;

// Global information about all tasks running on the system. This list is null-terminated.
extern pvdx_task_t task_list[];

// Exposed Functions
pvdx_task_t *get_task(TaskHandle_t id);
void init_task(size_t i);
void task_manager_init(void);
void task_manager_init_subtasks(void);
void task_manager_main(void *pvParameters);
void task_manager_exec(command_t cmd);


#endif // TASK_MANAGER_TASK_H