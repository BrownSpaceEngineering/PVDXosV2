#ifndef MAGNETOMETER_TASK_H
#define MAGNETOMETER_TASK_H

#include "magnetometer_hal.h"
#include <atmel_start.h>
#include <driver_init.h>
#include "globals.h"
#include "rtos_start.h"
#include "watchdog_task.h"
#include "string.h"
#include "logging.h"
#include "stdbool.h"

// FreeRTOS Task structs
// Memory for the magnetometer task
#define MAGNETOMETER_TASK_STACK_SIZE         1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
// ^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t magnetometer_task_stack[MAGNETOMETER_TASK_STACK_SIZE];
    StaticQueue_t magnetometer_task_queue;
    StaticTask_t magnetometer_task_tcb;
} magnetometer_task_memory_t;

extern magnetometer_task_memory_t magnetometer_mem;
extern uint8_t magnetometer_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];

// Queue for commands to be executed by the magnetometer task
extern QueueHandle_t magnetometer_command_queue_handle;

void exec_command_magnetometer(const command_t *p_cmd);
void main_magnetometer(void *pvParameters);

#endif // MAGNETOMETER_TASK_H