#ifndef WATCHDOG_TASK_H
#define WATCHDOG_TASK_H

// Includes
#include "atmel_start.h"
#include "globals.h"
#include "hardware_watchdog_utils.h"
#include "logging.h"
#include "rtos_start.h"
#include "task_manager_task.h"
#include "utils.h"

// Constants
#define WATCHDOG_MS_DELAY 1000 // Controls how often the Watchdog thread runs and verifies task checkins

// Memory for the watchdog task
#define WATCHDOG_TASK_STACK_SIZE 128 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
// ^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t watchdog_task_stack[WATCHDOG_TASK_STACK_SIZE];
    StaticTask_t watchdog_task_tcb;
} watchdog_task_memory_t;

extern watchdog_task_memory_t watchdog_mem;
extern volatile Wdt *const p_watchdog;
extern bool watchdog_enabled;

// Exposed functions
void init_watchdog();
void main_watchdog(void *pvParameters);
void early_warning_callback_watchdog(void);
void register_task_with_watchdog(TaskHandle_t handle);
void unregister_task_with_watchdog(TaskHandle_t handle);
void pet_watchdog(void);
void kick_watchdog(void);
// Exposed functions that go through the command dispatcher
void watchdog_checkin(void);

#endif // WATCHDOG_TASK_H