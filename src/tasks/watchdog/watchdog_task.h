#pragma once
#include "globals.h"
#include "atmel_start.h"

#define WATCHDOG_MS_DELAY 1000

// Initializes the memory for all the watchdog lists (i.e. for tasks to check in) and starts the watchdog timer
void watchdog_init(int watchdog_period, bool always_on);
void watchdog_main(void *pvParameters);

void watchdog_early_warning_callback();

// The following functions return 0 on success, -1 on failure
int watchdog_pet();
int watchdog_kick();

int watchdog_checkin(task_type_t task_id);
int watchdog_register_task(task_type_t task_id);
int watchdog_unregister_task(task_type_t task_id);
