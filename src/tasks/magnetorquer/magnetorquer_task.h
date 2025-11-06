/*
 * magnetorquer_task.h
 *
 * As of 20250921, this is just a skeleton for the magnetorquer task, which will
 * need a driver for the physical magnetorquers
 *
 * Author(s): Zach Mahan
 */

#ifndef MAGNETORQUER_TASK_H
#define MAGNETORQUER_TASK_H

#include <atmel_start.h>
#include <driver_init.h>

#include "globals.h"
#include "logging.h"
#include "rtos_start.h"
#include "stdbool.h"
#include "string.h"
#include "watchdog_task.h"

// FreeRTOS Task structs
// Memory for the magnetorquer task
#define MAGNETORQUER_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
// ^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t magnetorquer_task_stack[MAGNETORQUER_TASK_STACK_SIZE];
    uint8_t magnetorquer_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t magnetorquer_task_queue;
    StaticTask_t magnetorquer_task_tcb;
} magnetorquer_task_memory_t;

extern magnetorquer_task_memory_t magnetorquer_mem;

// Inputs voltages from ADCS navigation to be sent to magnetorquers
typedef struct {
    float x, y, z;
} magnetorquer_input_voltages_t;

QueueHandle_t init_magnetorquer(void);
void exec_command_magnetorquer(command_t *const p_cmd);
void main_magnetorquer(void *pvParameters);

void magnetorquer_update_voltages(magnetorquer_input_voltages_t voltages); // use inside of task, delegate to a driver

#endif // !MAGNETORQUER_TASK_H
