#ifndef UART_LISTENER_H
#define UART_LISTENER_H

// Includes
#include "globals.h"
#include "logging.h"
#include "queue.h"
#include "task_list.h"

// Constants
#define UART_LISTENER_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t uart_listener_task_stack[UART_LISTENER_TASK_STACK_SIZE];
    uint8_t uart_listener_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t uart_listener_task_queue;
    StaticTask_t uart_listener_task_tcb;
} uart_listener_task_memory_t;

extern uart_listener_task_memory_t uart_listener_mem;

QueueHandle_t init_uart_listener(void);
void main_uart_listener(void *pvParameters);

#endif // UART_LISTENER_H