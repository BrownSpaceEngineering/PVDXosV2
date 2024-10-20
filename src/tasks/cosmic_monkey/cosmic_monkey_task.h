#ifndef COSMIC_MONKEY_TASK_H
#define COSMIC_MONKEY_TASK_H

// Includes
#include <atmel_start.h>
#include <driver_init.h>
#include "globals.h"
#include "rtos_start.h"

// Constants
#define COSMIC_MONKEY_TASK_STACK_SIZE 128

typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t cosmic_monkey_task_stack[COSMIC_MONKEY_TASK_STACK_SIZE];
    StaticTask_t cosmic_monkey_task_tcb;
} cosmic_monkey_task_memory_t;

extern cosmic_monkey_task_memory_t cosmic_monkey_mem;

// Exposed Functions
status_t perform_flip();
void main_cosmic_monkey(void *pvParameters);

typedef struct {
    int frequency;
} cosmic_monkey_task_arguments_t;

#endif // COSMIC_MONKEY_TASK_H