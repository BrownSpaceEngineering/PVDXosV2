#ifndef WATCHDOG_TASK_H
#define WATCHDOG_TASK_H

#include "globals.h"
#include "atmel_start.h"
#include "hardware_watchdog_utils.h"
#include "SEGGER_RTT_printf.h"
#include "rtos_start.h"

#define WATCHDOG_MS_DELAY 1000

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

// If the difference between the current time and the time in the running_times array is greater than the allowed time,
// then the task has not checked in and the watchdog should reset the system. Refer to "globals.h" to see the order in which
// tasks are registered.
extern uint32_t allowed_times[NUM_TASKS];

extern volatile Wdt *const p_watchdog;
extern uint32_t running_times[NUM_TASKS];
extern bool should_checkin[NUM_TASKS];
extern bool watchdog_enabled;

// Initializes the memory for all the watchdog lists (i.e. for tasks to check in) and starts the watchdog timer
void watchdog_init(uint8_t watchdog_period, bool always_on);
void watchdog_main(void *pvParameters);
void watchdog_early_warning_callback(void);
void watchdog_pet(void);
void watchdog_kick(void);

// The following functions return 0 on success, -1 on failure
int watchdog_checkin(task_type_t task_index);
int watchdog_register_task(task_type_t task_index);
int watchdog_unregister_task(task_type_t task_index);

#endif // WATCHDOG_TASK_H