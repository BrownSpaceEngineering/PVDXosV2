#ifndef THERMISTOR_TASK_H
#define THERMISTOR_TASK_H

// Includes
#include <atmel_start.h>
#include <driver_init.h>

#include "globals.h"
#include "tasks/watchdog/watchdog_task.h"

#include "drivers/thermistor/thermistor_driver.h"

// Memory for the thermistor task
#define THERMISTOR_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t thermistor_task_stack[THERMISTOR_TASK_STACK_SIZE];
    StaticTask_t thermistor_task_tcb;
} thermistor_task_memory_t;

extern thermistor_task_memory_t thermistor_mem;

void main_thermistor(void *pvParameters);

#endif // THERMISTOR_TASK_H
