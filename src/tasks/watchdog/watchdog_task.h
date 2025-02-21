#ifndef WATCHDOG_TASK_H
#define WATCHDOG_TASK_H

// Includes
#include "atmel_start.h"
#include "globals.h"
#include "hardware_watchdog_utils.h"
#include "logging.h"
#include "rtos_start.h"
#include "task_manager_task.h"
#include "mutexes.h"

// Constants
#define WATCHDOG_MS_DELAY 1000 // Controls how often the Watchdog thread runs and verifies task checkins

// Memory for the watchdog task
#define WATCHDOG_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
// ^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t watchdog_task_stack[WATCHDOG_TASK_STACK_SIZE];
    StaticQueue_t watchdog_task_queue;
    StaticTask_t watchdog_task_tcb;
} watchdog_task_memory_t;

extern watchdog_task_memory_t watchdog_mem;
extern volatile Wdt *const p_watchdog;
extern QueueHandle_t watchdog_command_queue_handle;
extern uint8_t watchdog_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];

void watchdog_checkin(const TaskHandle_t handle);
void init_watchdog();
void main_watchdog(void *pvParameters);
void early_warning_callback_watchdog(void);
void pet_watchdog(void);
void kick_watchdog(void);
command_t get_watchdog_checkin_command(pvdx_task_t *const task);
void register_task_with_watchdog(const TaskHandle_t handle);
void unregister_task_with_watchdog(const TaskHandle_t handle);
void exec_command_watchdog(const command_t *const p_cmd);

#endif // WATCHDOG_TASK_H
