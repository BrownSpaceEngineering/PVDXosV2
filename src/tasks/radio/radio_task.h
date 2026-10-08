#ifndef RADIO_TASK_H
#define RADIO_TASK_H

#include <atmel_start.h>
#include <driver_init.h>

#include "FreeRTOS.h"
#include "globals.h"
#include "queue.h"

#define RADIO_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t radio_task_stack[RADIO_TASK_STACK_SIZE];
    uint8_t radio_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t radio_task_queue;
    StaticQueue_t uplink_queue_mem;
    StaticQueue_t downlink_queue_mem;
    StaticTask_t radio_task_tcb;
} radio_task_memory_t;

QueueHandle_t uplink_queue;
QueueHandle_t downlink_queue;

// Global memory and configuration
extern radio_task_memory_t radio_mem;

#define RADIO_BUFFER_SIZE 128

typedef struct {
    uint8_t target;
    uint8_t operation;
    char data[8]; // TODO: We need to decide how to represent the data to send up images.
    // Should "packeting" be done on the main board, or on the UHF board?
} uplink_data_t;

typedef struct {
    uint8_t target;
    uint8_t operation;
    uint8_t status;
    uint8_t data_type;
    char *data;
} downlink_data_t;

// Add functionality to handle case of data being pointed to yet not in radio?
#endif
