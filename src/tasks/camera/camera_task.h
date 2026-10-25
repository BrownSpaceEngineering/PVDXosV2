#ifndef CAMERA_TASK_H
#define CAMERA_TASK_H

#include "globals.h"

#define CAMERA_TASK_STACK_SIZE 1024 // Stack size in words

// Keep the TCB above the downward-growing stack, as with the other tasks.
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t camera_task_stack[CAMERA_TASK_STACK_SIZE];
    uint8_t camera_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t camera_task_queue;
    StaticTask_t camera_task_tcb;
} camera_task_memory_t;

extern camera_task_memory_t camera_mem;

QueueHandle_t init_camera(void);
void main_camera(void *pvParameters);
command_t get_camera_capture_command(void);
status_t camera_capture(void);
void exec_command_camera(command_t *const p_cmd);

bool camera_stop_requested(void);
bool camera_prepare_suspend(void);
void camera_prepare_resume(void);
// Camera task only: acknowledge a safe point and park without touching hardware.
void camera_park_if_stopped(void);

#endif // CAMERA_TASK_H
