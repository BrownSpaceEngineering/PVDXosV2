#ifndef FUEL_GAUGE_TASK_H
#define FUEL_GAUGE_TASK_H

// Includes
#include <atmel_start.h>
#include <driver_init.h>

#include "globals.h"
#include "tasks/watchdog/watchdog_task.h"

#include "drivers/fuel_gauge/fuel_gauge_driver.h"

// Memory for the fuel gauge task
#define FUEL_GAUGE_TASK_STACK_SIZE 512 // Size of the stack in words (multiply by 4 to get bytes)

// A pack temperature older than this isn't reported (the task reads the packs every 2 s)
#define FUEL_GAUGE_TEMP_MAX_AGE_MS 10000u
// Pack temperatures outside the MAX17205's operating range are taken to be bad reads
#define FUEL_GAUGE_TEMP_MIN_C (-40.0f)
#define FUEL_GAUGE_TEMP_MAX_C 85.0f

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t fuel_gauge_task_stack[FUEL_GAUGE_TASK_STACK_SIZE];
    StaticTask_t fuel_gauge_task_tcb;
} fuel_gauge_task_memory_t;

extern fuel_gauge_task_memory_t fuel_gauge_mem;

void main_fuel_gauge(void *pvParameters);

// Writes the mean temperature of the packs that read on the task's last cycle and returns true, or returns false if no
// pack has read in the last FUEL_GAUGE_TEMP_MAX_AGE_MS. Safe to call from any task.
bool fuel_gauge_get_temperature(float *const p_temp_c);

#endif // FUEL_GAUGE_TASK_H
