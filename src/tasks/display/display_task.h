#ifndef DISPLAY_TASK_H
#define DISPLAY_TASK_H

// Includes
#include "atmel_start.h"
#include "globals.h"
#include "watchdog_task.h"
#include "display_hal.h"
#include "image_buffer_BrownLogo.h"
#include "image_buffer_PVDX.h"
#include "logging.h"

// FreeRTOS Task structs
// Memory for the display task
#define DISPLAY_TASK_STACK_SIZE         1024 // Size of the stack in words (multiply by 4 to get bytes)

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

void init_display_task(void);
void main_display(void *pvParameters);
status_t display_image(const color_t* p_buffer);
status_t clear_image(void);
status_t init_display(void);
status_t display_update(void);
void display_set_buffer(const color_t* p_buffer);
void display_clear_buffer(void);

#endif // DISPLAY_TASK_H