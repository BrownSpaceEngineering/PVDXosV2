#ifndef ADCS_TASK_H
#define ADCS_TASK_H

#include <atmel_start.h>
#include <driver_init.h>
#include "globals.h"
#include "logging.h"
#include "rtos_start.h"
#include "stdbool.h"
#include "string.h"
#include "watchdog_task.h"

// FreeRTOS Task structs
// Memory for the ADCS task
#define ADCS_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
// ^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t adcs_task_stack[ADCS_TASK_STACK_SIZE];
    uint8_t adcs_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t adcs_task_queue;
    StaticTask_t adcs_task_tcb;
} adcs_task_memory_t;

extern adcs_task_memory_t adcs_mem;

QueueHandle_t init_adcs(void);
void exec_command_adcs(command_t *const p_cmd);
void main_adcs(void *pvParameters);

#endif // ADCS_TASK_H
