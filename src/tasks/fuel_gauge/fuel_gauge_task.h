#ifndef FUEL_GAUGE_TASK_H
#define FUEL_GAUGE_TASK_H

// Includes
#include <atmel_start.h>
#include <driver_init.h>

#include "globals.h"
#include "rtos_start.h"
#include "watchdog_task.h"

#include "fuel_gauge_driver.h"

// Memory for the fuel gauge task
#define FUEL_GAUGE_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
// ^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t fuel_gauge_task_stack[FUEL_GAUGE_TASK_STACK_SIZE];
    StaticTask_t fuel_gauge_task_tcb;
} fuel_gauge_task_memory_t;

extern fuel_gauge_task_memory_t fuel_gauge_mem;

void main_fuel_gauge(void *pvParameters);

#endif // FUEL_GAUGE_TASK_H
