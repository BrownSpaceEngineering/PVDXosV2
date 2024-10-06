#ifndef COSMIC_MONKEY_TASK_H
#define COSMIC_MONKEY_TASK_H

// Includes
#include <atmel_start.h>
#include <driver_init.h>
#include "globals.h"
#include "rtos_start.h"

// Constants
#define COSMIC_MONKEY_TASK_STACK_SIZE 128

struct CosmicMonkeyTaskMemory {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t cosmic_monkey_task_stack[COSMIC_MONKEY_TASK_STACK_SIZE];
    StaticTask_t cosmic_monkey_task_tcb;
};

extern struct CosmicMonkeyTaskMemory cosmic_monkey_mem;

// Exposed Functions
Status perform_flip();
void cosmic_monkey_main(void *pvParameters);

typedef struct CosmicMonkeyTaskArguments {
    int frequency;
} CosmicMonkeyTaskArguments;

#endif // COSMIC_MONKEY_TASK_H