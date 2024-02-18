#ifndef WATCHDOG_TASK_H
#define WATCHDOG_TASK_H

#include "globals.h"
#include "atmel_start.h"

#define WATCHDOG_MS_DELAY 1000

// If the difference between the current time and the time in the running_times array is greater than the allowed time,
// then the task has not checked in and the watchdog should reset the system. Refer to "globals.h" to see the order in which
// tasks are registered.
static uint32_t allowed_times[NUM_TASKS] = {1000, 1000};

static volatile Wdt *watchdog = WDT;
static uint32_t running_times[NUM_TASKS];
static bool should_checkin[NUM_TASKS];
static bool watchdog_enabled = false;

// Initializes the memory for all the watchdog lists (i.e. for tasks to check in) and starts the watchdog timer
void watchdog_init(int watchdog_period, bool always_on);
void watchdog_main(void *pvParameters);
void watchdog_early_warning_callback(void);
void watchdog_pet(void);
void watchdog_kick(void);

// The following functions return 0 on success, -1 on failure
int watchdog_checkin(task_type_t task_index);
int watchdog_register_task(task_type_t task_index);
int watchdog_unregister_task(task_type_t task_index);

#endif // WATCHDOG_TASK_H