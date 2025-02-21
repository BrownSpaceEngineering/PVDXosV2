#ifndef DISPLAY_TASK_H
#define DISPLAY_TASK_H

// Includes
#include "atmel_start.h"
#include "display_hal.h"
#include "globals.h"
#include "image_buffer_BrownLogo.h"
#include "image_buffer_PVDX.h"
#include "logging.h"
#include "watchdog_task.h"

// FreeRTOS Task structs
// Memory for the display task
#define DISPLAY_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t display_task_stack[DISPLAY_TASK_STACK_SIZE];
    uint8_t display_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t display_task_queue;
    StaticTask_t display_task_tcb;
} display_task_memory_t;

extern display_task_memory_t display_mem;

void main_display(void *pvParameters);
status_t display_image(const color_t *const p_buffer);
status_t clear_image(void);
QueueHandle_t init_display(void);
status_t display_update(void);
void display_set_buffer(const color_t *const p_buffer);
void display_clear_buffer(void);
command_t get_display_image_command(const color_t *const p_buffer, status_t *const p_result);
void exec_command_display(command_t *const p_cmd);

#endif // DISPLAY_TASK_H