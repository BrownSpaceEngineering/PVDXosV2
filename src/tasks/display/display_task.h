#ifndef DISPLAY_TASK_H
#define DISPLAY_TASK_H

// Includes
#include "watchdog_task.h"
#include <atmel_start.h>
#include <globals.h>
#include "display_hal.h"

// FreeRTOS Task structs
// Memory for the display task
#define DISPLAY_TASK_STACK_SIZE         128 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t displayTaskStack[DISPLAY_TASK_STACK_SIZE];
    StaticQueue_t displayTaskQueue;
    StaticTask_t displayTaskTCB;
} display_task_memory_t;

extern display_task_memory_t display_mem;

// Queue for commands to be executed by the display task
extern QueueHandle_t displayQueue;

// Exposed Functions
void display_main(void *pvParameters);

#endif // DISPLAY_TASK_H