#ifndef SHELL_TASK_H
#define SHELL_TASK_H

// Includes
#include <atmel_start.h>
#include <driver_init.h>

#include "globals.h"
#include "rtos_start.h"
#include "watchdog_task.h"

// Constants
#define SHELL_ASCII_ART                                                                                                                    \
    " ______     ________  __\n"                                                                                                           \
    "|  _ \\ \\   / /  _ \\ \\/ /___  ___  \n"                                                                                             \
    "| |_) \\ \\ / /| | | \\  // _ \\/ __| \n"                                                                                             \
    "|  __/ \\ V / | |_| /  \\ (_) \\__ \\ \n"                                                                                             \
    "|_|     \\_/  |____/_/\\_\\___/|___/  \n"

#define SHELL_INPUT_POLLING_INTERVAL 200 // Check for new commands through RTT this often (in ms)
#define SHELL_INPUT_BUFFER_SIZE 128
#define MAX_ARGS 10
#define SHELL_PROMPT (RTT_CTRL_TEXT_GREEN "PVDXos Shell> $ " RTT_CTRL_RESET)
#define SHELL_RTT_CHANNEL 0 /* CHANGE THIS WITH CAUTION! GetKey AND PutKey ARE NOT GUARANTEED TO WORK ON CHANNELS OTHER THAN ZERO */

// Memory for the shell task
#define SHELL_TASK_STACK_SIZE 128 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t shell_task_stack[SHELL_TASK_STACK_SIZE];
    StaticTask_t shell_task_tcb;
} shell_task_memory_t;

extern shell_task_memory_t shell_mem;

void main_shell(void *pvParameters);

// Defines that will never run in practice, but are useful for the IDE not throwing a billion errors:

#endif // SHELL_TASK_H