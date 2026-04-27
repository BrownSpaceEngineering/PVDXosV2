#ifndef CFDP_TASK_H
#define CFDP_TASK_H

// Includes
#include <atmel_start.h>
#include <driver_init.h>

#include "globals.h"
#include "tasks/cfdp/cfdp_engine.h"
#include "tasks/watchdog/watchdog_task.h"

// Memory for the CFDP task
#define CFDP_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

#define CFDP_MAX_ACTIVE_TRANSACTIONS 4

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t cfdp_task_stack[CFDP_TASK_STACK_SIZE];
    uint8_t cfdp_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t cfdp_task_queue;
    StaticTask_t cfdp_task_tcb;
} cfdp_task_memory_t;

typedef enum cfdp_txn_type {
    IMAGE = 0,
    TELEMETRY = 1
} cfdp_txn_type_t;

typedef union {
    uint32_t txn_id;
    cfdp_txn_type_t txn_type;
} cfdp_request_data_t;

typedef enum cfdp_request_data_type {
    CFDP_PUT_REQ = 0,
    CFDP_CANCEL_REQ = 1
} cfdp_request_data_type_t;

struct cfdp_request {
    cfdp_request_data_type_t type;
    cfdp_request_data_t data;
};

extern cfdp_task_memory_t cfdp_mem;

int cfdp_put_request(cfdp_txn_type_t type);

int cfdp_cancel_request(uint32_t txn_id);

QueueHandle_t init_cfdp(void);
void main_cfdp(void *pvParameters);

#endif // CFDP_TASK_H
