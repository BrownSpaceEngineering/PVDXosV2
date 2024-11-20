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
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t display_task_stack[DISPLAY_TASK_STACK_SIZE];
    StaticQueue_t display_task_queue;
    StaticTask_t display_task_tcb;
} display_task_memory_t;

extern display_task_memory_t display_mem;
extern uint8_t display_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];

// Queue for commands to be executed by the display task
extern QueueHandle_t display_command_queue_handle;

// Exposed Functions
void init_display_task(void);
void main_display(void *pvParameters);

#endif // DISPLAY_TASK_H