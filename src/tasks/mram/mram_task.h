#ifndef MRAM_TASK_H
#define MRAM_TASK_H

#include <atmel_start.h>
#include <driver_init.h>

#include "globals.h"
#include "logging.h"
#include "rtos_start.h"
#include "task_list.h"
#include "watchdog_task.h"

typedef struct {
    long pos;
    long len;
    char *buf;
    char write;
} mram_request_t;

extern mram_task_memory_t mram_mem;

// FreeRTOS Task structs
// Memory for the MRAM task
#define MRAM_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
// ^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t mram_task_stack[MRAM_TASK_STACK_SIZE];
    uint8_t mram_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t mram_task_queue;
    StaticTask_t mram_task_tcb;
} mram_task_memory_t;

QueueHandle_t init_mram(void);
void exec_command_mram(command_t *const p_cmd);
void main_mram(void *pvParameters);

#endif