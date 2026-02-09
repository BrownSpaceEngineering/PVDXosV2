#ifndef ARDUCAM_TASK_H
#define ARDUCAM_TASK_H

// Includes
#include "arducam_driver.h"
#include "globals.h"
#include "logging.h"
#include "watchdog_task.h"
#include "arducam_registers.h"

// FreeRTOS Task structs
// Memory for the arducam task
#define ARDUCAM_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Separate RTT channel for streaming camera image bytes for debugging
#define RTT_CAMERA_OUTPUT_CHANNEL 2
#define SEGGER_RTT_IMAGE_BUFFER_SIZE 4096
extern uint8_t SEGGER_RTT_IMAGE_BUFFER[SEGGER_RTT_IMAGE_BUFFER_SIZE];

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t arducam_task_stack[ARDUCAM_TASK_STACK_SIZE];
    uint8_t arducam_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t arducam_task_queue;
    StaticTask_t arducam_task_tcb;
} arducam_task_memory_t;

extern arducam_task_memory_t arducam_mem;

void main_arducam(void *pvParameters);
QueueHandle_t init_arducam(void);
// void exec_command_arducam(command_t *const p_cmd); // TODO: implement later if commands are added

#endif // ARDUCAM_TASK_H
