#ifndef CFDP_TASK_H
    #define CFDP_TASK_H

    // Includes
    #include <atmel_start.h>
    #include <driver_init.h>
    #include <stdbool.h>
    #include <stddef.h>
    #include <stdint.h>
    #include <string.h>

    #include "drivers/at86rf215/at86rf215.h"
    #include "globals.h"
    #include "tasks/cfdp/cfdp_pdu.h"
    #include "tasks/watchdog/watchdog_task.h"

typedef struct at86rf215 at86rf215_t;

    // Memory for the CFDP task
    #define CFDP_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

    #define CFDP_MAX_ACTIVE_TRANSACTIONS 4

    #define MAX_FILE_SIZE 4096 // placeholder
    #define SEGMENT_SIZE 32    // placeholder

    #define REQ_CLOSURE 0   // 1 : requested, 0 : not requested
    #define CHECKSUM_TYPE 0 // Default to 0 (Modular Checksum)

    #define ACK_TIMEOUT_MS 10000UL // placeholders
    #define NAK_TIMEOUT_MS 10000UL
    #define TRANSACTION_LIFETIME_MS 43200000UL
    #define PROMPT_TIMEOUT_MS 20000UL

    #define ACK_RETRANSMIT_LIMIT 8
    #define NAK_RETRANSMIT_LIMIT 8
    #define MAX_TRANSACTIONS 8

    #define CFDP_MAX_SEGMENT_REQUESTS 16

    #define TX_TIMEOUT_MS 5000UL // max time to wait for a single PDU transmission to complete

    #define IMAGE_BUF 0xFFFFFFFF // placeholder, this probably isn't gonna be how we represent it
    #define TELEMETRY_BUF 0XFFFFFFFF

    // Placeholder sizes until real image/telemetry sources are wired in.
    #define IMAGE_FILE_SZ MAX_FILE_SIZE
    #define TELEMETRY_FILE_SZ MAX_FILE_SIZE

    #define TXN_FRAME 0x1000

    // Received-segment bitmap sizing.
    // One bit per SEGMENT_SIZE-byte chunk, covering the worst-case MAX_FILE_SIZE file.
    #define CFDP_MAX_SEGMENTS (MAX_FILE_SIZE / SEGMENT_SIZE) // 128 segments
    #define CFDP_BITMAP_WORDS (CFDP_MAX_SEGMENTS / 32)       // 4 × uint32_t = 16 bytes

    #define CFDP_LARGE_BUFF_SZ 8192
    #define CFDP_SMALL_BUFF_SZ 128
    #define CFDP_SMALL_BUFF_COUNT 4

    #define CFDP_MAX_PDU_SIZE 128

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

    CFDP_RECV_STATE_WAIT_EOF,
    CFDP_RECV_STATE_SEND_NAK,
    CFDP_RECV_STATE_WAIT_RETRANSMIT,
    CFDP_RECV_STATE_SEND_FIN,
    CFDP_RECV_STATE_WAIT_FIN_ACK,
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
    CFDP_RESULT_BLOCKED,
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
    uint8_t eof_retransmit_counter;
    uint8_t nak_retransmit_counter;

    uint32_t checksum;
    uint8_t checksum_type;

    cfdp_nak_buf_t nak_buf;

    uint32_t file_size;
    uint32_t file_offset;

    cfdp_state_t state;
    cfdp_direction_t direction;

    bool reliable_mode;

    uint8_t channel_num;
    uint8_t priority;

    uint8_t *file_data;

    // One bit per segment (SEGMENT_SIZE bytes). Bit i is set when segment i has been received.
    // Used on the receive side to detect gaps without relying on zero-byte content checks.
    uint32_t received_bitmap[CFDP_BITMAP_WORDS];

    cfdp_lv_t source_filename;
    cfdp_lv_t dest_filename;

    // Checksum received from the sender's EOF PDU. Stored for audit/logging after transfer completes.
    uint32_t expected_checksum;

    at86rf215_t *radio_handle; // handle to the radio hardware; channel_num selects RF09 vs RF24

} cfdp_transaction_t;

typedef struct {
    cfdp_transaction_t transactions[MAX_TRANSACTIONS];
    bool active[MAX_TRANSACTIONS];
    bool slot_free;
} cfdp_transaction_store_t;

typedef struct {
    uint8_t buff[CFDP_LARGE_BUFF_SZ];
    bool in_use;
} cfdp_large_buff_t;

typedef struct {
    uint8_t buff[CFDP_SMALL_BUFF_SZ * CFDP_SMALL_BUFF_COUNT];
    bool in_use[CFDP_SMALL_BUFF_COUNT];
} cfdp_small_buffs_t;

extern cfdp_task_memory_t cfdp_mem;

extern cfdp_transaction_store_t cfdp_txn_store;

extern cfdp_large_buff_t cfdp_large_buff;
extern cfdp_small_buffs_t cfdp_small_buffs;

uint32_t next_seq_num(void);

void uint32_to_big_endian(uint32_t src, uint8_t dst[4]);
void uint16_to_big_endian(uint16_t src, uint8_t dst[2]);

void cfdp_nak_buf_push(cfdp_nak_buf_t *buf, cfdp_pdu_segment_request_t segment);
cfdp_pdu_segment_request_t cfdp_nak_buf_pop(cfdp_nak_buf_t *buf);
size_t cfdp_nak_buf_get_index(cfdp_nak_buf_t *buf, size_t offset, size_t len);

cfdp_transaction_t *cfdp_alloc_transaction(cfdp_transaction_store_t *txn_store);
void cfdp_free_transaction(cfdp_transaction_store_t *txn_store, cfdp_transaction_t *txn);
cfdp_transaction_t *cfdp_find_transaction(cfdp_transaction_store_t *txn_store, uint32_t entity_id, uint32_t seq_num);

uint8_t *cfdp_alloc_small_buff();
uint8_t *cfdp_alloc_large_buff();
int cfdp_free_buff(uint8_t *buff);

cfdp_transaction_t *cfdp_send_init(cfdp_transaction_store_t *txn_store, uint8_t *fl, uint32_t sz, cfdp_lv_t source_filename,
                                   cfdp_lv_t dest_filename, uint32_t source_entity_id, uint32_t dest_entity_id, uint8_t channel_num,
                                   uint8_t priority, bool reliable_mode, at86rf215_t *radio_handle);
int cfdp_send_init_simple(uint8_t *fl, size_t sz, at86rf215_t *radio_handle, cfdp_transaction_store_t *txn_store);

cfdp_result_t cfdp_handle_send_state(cfdp_transaction_t *transaction, uint32_t elapsed_ms);
cfdp_result_t cfdp_handle_recv_state(cfdp_transaction_t *transaction, uint32_t elapsed_ms);
cfdp_result_t cfdp_transact(cfdp_transaction_t *txn, uint32_t elapsed_ms);

void cfdp_put_request(cfdp_txn_type_t type, cfdp_direction_t dir);
void cfdp_cancel_request(uint32_t txn_id);

void cfdp_send(cfdp_transaction_t *transaction, const uint8_t *buff, size_t sz);
int send(void *buff, size_t sz);
int recv(void *buff, size_t sz);

int cfdp_prepare_pdu_header(uint8_t *buff, cfdp_transaction_t *transaction, uint16_t pdu_len, cfdp_pdu_type_t pdu_type);
int cfdp_send_metadata(cfdp_transaction_t *transaction);
int cfdp_send_filedata(cfdp_transaction_t *transaction, uint32_t offset, uint32_t size);
uint32_t cfdp_calculate_modular_checksum(cfdp_transaction_t *transaction);
int cfdp_send_fin(cfdp_transaction_t *transaction, uint8_t condition_code);
int cfdp_send_ack(cfdp_transaction_t *transaction, uint8_t acked_directive_code, uint8_t directive_subtype_code, uint8_t condition_code,
                  uint8_t transaction_status);
int cfdp_send_eof(cfdp_transaction_t *transaction, uint8_t condition_code);
int cfdp_send_nak(cfdp_transaction_t *transaction);
int cfdp_resend(cfdp_transaction_t *transaction);

void exec_command_cfdp_request(command_t *const p_cmd);
void cfdp_process_pdu(uint8_t *raw, size_t sz);

QueueHandle_t init_cfdp(void);
void main_cfdp(void *pvParameters);

#endif // CFDP_TASK_H

/**
 * CFDP TODO
 * - Make sure we reject incoming files with large file flag set (might already happen since we'll never have a buff that big)
 * - Make it so unacknowledged transaction are actually run in that mode
 * - Implement timers with rtc
 * - more ...
 */
