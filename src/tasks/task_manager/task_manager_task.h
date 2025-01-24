#ifndef TASK_MANAGER_TASK_H
#define TASK_MANAGER_TASK_H

// Includes
#include <atmel_start.h>
#include <driver_init.h>
#include "globals.h"
#include "logging.h"
#include "mutexes.h"

// Constants
#define TASK_MANAGER_TASK_STACK_SIZE 128               // Size of the stack in words (multiply by 4 to get bytes)
#define TASK_MANAGER_QUEUE_WAIT_MS 1000                // Wait time for sending/receiving a command to/from the queue (in ms)

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
extern uint8_t task_manager_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
extern QueueHandle_t task_manager_command_queue_handle;

// Mutex related variables
extern SemaphoreHandle_t task_list_mutex;
extern StaticSemaphore_t task_list_mutex_buffer;

void init_task(size_t i);
void init_task_manager(void);
void main_task_manager(void *pvParameters);
void exec_command_task_manager(command_t cmd);
void task_manager_init_subtasks(void);
void task_manager_enable_task(pvdx_task_t* task);
void task_manager_disable_task(pvdx_task_t* task);

#endif // TASK_MANAGER_TASK_H