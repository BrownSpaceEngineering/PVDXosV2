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

#define IMAGE_BUF 0xFFFFFFFF // placeholder, this probably isn't gonna be how we represent it
#define TELEMETRY_BUF 0XFFFFFFFF

#define TXN_FRAME 0x1000

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

typedef union cfdp_request_data {
    uint32_t txn_id;
    cfdp_txn_type_t txn_type;
} cfdp_request_data_t;

typedef enum cfdp_state {
    CFDP_SEND_STATE_METADATA_SEND = 0,
    CFDP_SEND_STATE_FILE_SEND,
    CFDP_SEND_STATE_WAIT_ACK,
    CFDP_SEND_STATE_WAIT_FIN,
    CFDP_SEND_STATE_DONE,
    CFDP_SEND_STATE_ERR,

    CFDP_RECV_STATE_FILE_RECV,
    CFDP_RECV_STATE_SEND_NAK,
    CFDP_RECV_STATE_WAIT_ACK,
    CFDP_RECV_STATE_SEND_FIN,
    CFDP_RECV_STATE_DONE,
    CFDP_RECV_STATE_ERR
} cfdp_state_t;

typedef enum cfdp_direction {
    CFDP_SEND = 0,
    CFDP_RECV
} cfdp_direction_t;

typedef enum cfdp_pdu_type {
    CFDP_FILE_DIRECTIVE = 0,
    CFDP_FILE_DATA
} cfdp_pdu_type_t;

typedef enum cfdp_result {
    CFDP_RESULT_IN_PROGRESS = 0,
    CFDP_RESULT_COMPLETE,
    CFDP_RESULT_ERROR,
    CFDP_RESULT_INVALID_ARG
} cfdp_result_t;

typedef struct cfdp_nak_buf {
    cfdp_pdu_segment_request_t segments[CFDP_MAX_SEGMENT_REQUESTS];
    uint32_t tail;
    uint32_t head;
    uint32_t size;
} cfdp_nak_buf_t;

typedef struct cfdp_transaction {
    cfdp_transaction_id_t transaction_id;
    uint32_t dest_entity_id;

    uint32_t inactivity_timer;
    uint32_t ack_timer;
    uint32_t nak_timer;

    uint32_t checksum;
    uint8_t checksum_mode;

    uint8_t eof_retransmit_counter;
    uint8_t nak_retransmit_counter;

    cfdp_nak_buf_t nak_buf;

    uint32_t file_size;
    uint32_t file_offset;

    cfdp_state_t state;
    cfdp_direction_t direction;

    bool reliable_mode;

    uint8_t channel_num;
    uint8_t priority;

    uint8_t *file_data;

    cfdp_lv_t source_filename;
    cfdp_lv_t dest_filename;

    at86rf215_t *radio_handle; // handle to the radio hardware; channel_num selects RF09 vs RF24

} cfdp_transaction_t;

typedef struct {
    cfdp_transaction_t transactions[MAX_TRANSACTIONS];
    bool active[MAX_TRANSACTIONS];
    bool slot_free;
} cfdp_transaction_store_t;

extern cfdp_task_memory_t cfdp_mem;

extern cfdp_transaction_store_t cfdp_txn_store;

size_t cfdp_put_request(cfdp_txn_type_t type);

void cfdp_cancel_request(uint32_t txn_id);

int send(void *buff, size_t sz);
int recv(void *buff, size_t sz);

void cfdp_process_pdu(uint8_t *raw, size_t sz);

size_t cfdp_process_crc(uint8_t *raw, size_t pdu_sz);

QueueHandle_t init_cfdp(void);
void main_cfdp(void *pvParameters);

#endif // CFDP_TASK_H
