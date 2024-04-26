#ifndef WATCHDOG_TASK_H
#define WATCHDOG_TASK_H

// Includes
#include "globals.h"
#include "atmel_start.h"
#include "hardware_watchdog_utils.h"
#include "logging.h"
#include "rtos_start.h"
#include "task_manager_task.h"

// Constants
#define WATCHDOG_MS_DELAY 1000 // Controls how often the Watchdog thread runs and verifies task checkins

// Memory for the watchdog task
#define WATCHDOG_TASK_STACK_SIZE 128 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
// ^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
struct watchdogTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t watchdogTaskStack[WATCHDOG_TASK_STACK_SIZE];
    StaticTask_t watchdogTaskTCB;
};

extern struct watchdogTaskMemory watchdogMem;
extern volatile Wdt *const p_watchdog;
extern bool watchdog_enabled;

void watchdog_init(uint8_t watchdog_period, bool always_on);
void watchdog_main(void *pvParameters);
void watchdog_early_warning_callback(void);
void watchdog_pet(void);
void watchdog_kick(void);
void watchdog_checkin(void);
void watchdog_register_task(TaskHandle_t handle);
void watchdog_unregister_task(TaskHandle_t handle);

#endif // WATCHDOG_TASK_H