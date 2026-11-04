#ifndef ARDUCAM_TASK_H
#define ARDUCAM_TASK_H

// Includes
#include <atmel_start.h>
#include <driver_init.h>

#include "globals.h"
#include "tasks/watchdog/watchdog_task.h"

#include "drivers/arducam/arducam_driver.h"

// Memory for the arducam task
#define ARDUCAM_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t arducam_task_stack[ARDUCAM_TASK_STACK_SIZE];
    StaticTask_t arducam_task_tcb;
} arducam_task_memory_t;

extern arducam_task_memory_t arducam_mem;

void main_arducam(void *pvParameters);

#endif // ARDUCAM_TASK_H
