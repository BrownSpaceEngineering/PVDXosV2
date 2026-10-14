#ifndef MAGNETOMETER_TASK_H
#define MAGNETOMETER_TASK_H

// Includes
#include "globals.h"
#include "logging.h"
#include "watchdog_task.h"
#include "magnetometer_driver.h"

// FreeRTOS Task structs
// Memory for the magnetometer task
#define MAGNETOMETER_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t magnetometer_task_stack[MAGNETOMETER_TASK_STACK_SIZE];
    uint8_t magnetometer_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t magnetometer_task_queue;
    StaticTask_t magnetometer_task_tcb;
} magnetometer_task_memory_t;

extern magnetometer_task_memory_t magnetometer_mem;

void main_magnetometer(void *pvParameters);
QueueHandle_t init_magnetometer(void);

#endif // MAGNETOMETER_TASK_H
