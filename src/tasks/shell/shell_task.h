#ifndef SHELL_TASK_H
#define SHELL_TASK_H

#include "globals.h"
#include "rtos_start.h"
#include "watchdog_task.h"

#include <atmel_start.h>
#include <driver_init.h>

#define SHELL_INPUT_POLLING_INTERVAL 250 // Check for new commands through RTT this often (in ms)
#define SHELL_INPUT_BUFFER_SIZE      128
#define MAX_ARGS                     10
#define SHELL_PROMPT                 (RTT_CTRL_TEXT_GREEN "PVDXos Shell> $ " RTT_CTRL_TEXT_BRIGHT_WHITE)
#define SHELL_RTT_CHANNEL            0 /* CHANGE THIS WITH CAUTION! GetKey AND PutKey ARE NOT GUARANTEED TO WORK ON CHANNELS OTHER THAN ZERO */

// Memory for the shell task
#define SHELL_TASK_STACK_SIZE 128 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
struct shellTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t shellTaskStack[SHELL_TASK_STACK_SIZE];
    StaticTask_t shellTaskTCB;
};

extern struct shellTaskMemory shellMem;

void shell_main(void *pvParameters);


#endif // SHELL_TASK_H