#ifndef CFDP_TASK_H
#define CFDP_TASK_H

// Includes
#include <atmel_start.h>
#include <driver_init.h>
#include <semphr.h>

#include "ccsds/radio_routing.h"
#include "cfdp_pdu.h"
#include "drivers/at86rf215/at86rf215.h"
#include "drivers/mram/mram_map.h"
#include "globals.h"
#include "tasks/watchdog/watchdog_task.h"
#include "timers.h"

// Memory for the CFDP task
#define CFDP_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

#define MAX_FILE_SIZE 4096 // placeholder
#define SEGMENT_SIZE 492   // (512 - 20)  // placeholder

#define REQ_CLOSURE 0   // 1 : requested, 0 : not requested
#define CHECKSUM_TYPE 0 // Default to 0 (Modular Checksum)

#define CFDP_MAX_SEGMENT_REQUESTS 16
#define CFDP_MAX_SEGMENTS (MAX_FILE_SIZE / SEGMENT_SIZE) // 128 segments

// Placeholder sizes until real image/telemetry sources are wired in.
#define IMAGE_FILE_SZ MAX_FILE_SIZE
#define TELEMETRY_FILE_SZ MAX_FILE_SIZE

#define TXN_FRAME 0x200 // placeholder

#define CFDP_LARGE_BUFF_SZ 10240 // fits an uplink with a display bitmap (16 + 6 + 8192 bytes)
#define CFDP_SMALL_BUFF_SZ 128
#define CFDP_SMALL_BUFF_COUNT 4

#define CFDP_MAX_PDU_SIZE 1024 // double what we expect so hmm.

// How long the main loop waits for a command (idle) or a notification (busy) before servicing received PDUs
#define CFDP_POLL_MS 10

// Task notification bits. The inactivity timeout and cancels can't go through the command queue, since it's only read
// while no transaction is active
#define CFDP_NOTIFY_INACTIVITY (1UL << 0)
#define CFDP_NOTIFY_CANCEL (1UL << 1)
#define CFDP_NOTIFY_ALL (CFDP_NOTIFY_INACTIVITY | CFDP_NOTIFY_CANCEL)
// Placed in a struct to ensure that the TCB is placed higher than the stack in
// memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack
// grows downwards)
//
// The inactivity timer is created once by init_cfdp() and reused by every transaction. Retransmits are counted in link
// turns instead (see radio_routing.h)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t cfdp_task_stack[CFDP_TASK_STACK_SIZE];
    uint8_t cfdp_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t cfdp_task_queue;
    StaticTask_t cfdp_task_tcb;

    StaticTimer_t inactivity_timer_mem;
    TimerHandle_t inactivity_timer_handle;
} cfdp_task_memory_t;

typedef enum cfdp_state {
    CFDP_SEND_STATE_METADATA_SEND = 0,
    CFDP_SEND_STATE_FILE_SEND,
    CFDP_SEND_STATE_WAIT_ACK,
    CFDP_SEND_STATE_WAIT_FIN,
    CFDP_SEND_STATE_DONE,
    CFDP_SEND_STATE_ERR,

    CFDP_RECV_STATE_MISSING_MD,
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
    CFDP_RESULT_DOWNLINK_FULL, // a PDU couldn't be queued for downlink; retried next pass without waiting for a received PDU
    CFDP_RESULT_COMPLETE,
    CFDP_RESULT_ERROR,
    CFDP_RESULT_INVALID_ARG
} cfdp_result_t;

typedef struct cfdp_buff_header {
    size_t ref_count;
    StaticSemaphore_t _semaphore_mem;
    SemaphoreHandle_t semaphore;
    bool is_large;
    void *buff;
} cfdp_buff_header_t;

typedef struct {
    cfdp_buff_header_t header;
    uint8_t buff[CFDP_LARGE_BUFF_SZ];
} cfdp_large_buff_t;

typedef struct {
    cfdp_buff_header_t header;
    uint8_t buff[CFDP_SMALL_BUFF_SZ];
} cfdp_small_buff_t;

typedef struct cfdp_nak_buf {
    cfdp_pdu_segment_request_t segments[CFDP_MAX_SEGMENT_REQUESTS];
    uint32_t tail;
    uint32_t head;
    uint32_t size;
} cfdp_nak_buf_t;

typedef struct cfdp_transaction {
    cfdp_transaction_id_t transaction_id;
    uint32_t dest_entity_id;

    // Checksum received from the sender's EOF PDU. Stored for audit/logging
    // after transfer completes.
    uint32_t expected_checksum;

    // Stream the file belongs to, which picks the radio and VC its file PDUs are sent on. Replies always go on
    // RADIO_STREAM_CFDP_REPLY
    radio_stream_t stream;

    // Pass the transaction started in. It's dropped when that pass ends
    uint32_t pass;

    // Retransmit (EOF, FIN or NAK) at the start of our turn number retransmit_turn, if armed. Covers the ACK-wait and
    // NAK-wait roles, see cfdp_turn_retransmit()
    bool retransmit_armed;
    uint32_t retransmit_turn;

    uint8_t ack_retransmit_counter;
    uint8_t nak_retransmit_counter;

    uint32_t checksum;
    uint8_t checksum_type;

    cfdp_nak_buf_t nak_buf;

    uint32_t file_size;
    uint32_t file_offset;

    cfdp_state_t state;
    cfdp_direction_t direction;
    uint8_t condition_code;

    bool reliable_mode;
    bool delivery_complete;
    bool eof_acked;

    cfdp_buff_header_t *buff_header;
    // The file in RAM. Received files always use it. When it's NULL, a sent file is read from MRAM instead: file_size
    // bytes at file_mram_offset within file_mram_region (see cfdp_put_request). Read either with cfdp_read_file()
    uint8_t *file_data;
    uint8_t file; // Sent files: the cfdp_file_t being sent, so its state machine input is cleared once it's delivered
    mram_region_t file_mram_region;
    uint32_t file_mram_offset;

    // Sent files: `checksum` is the modular checksum of the file's first checksum_offset bytes, added to as file data
    // is first sent, so the EOF doesn't have to read the whole file again (slow from MRAM)
    uint32_t checksum_offset;

    uint32_t latest_packet_timestamp;

    cfdp_lv_t source_filename;
    cfdp_lv_t dest_filename;
} cfdp_transaction_t;

// CFDP runs one transaction at a time
typedef struct {
    cfdp_transaction_t txn;
    bool active;

    // Sequence number of the last incoming transfer accepted. An incoming transfer only overrides an active
    // transaction if its sequence number is newer, so a delayed PDU from an old transfer can't
    bool have_ground_seq;
    uint32_t last_ground_seq;
} cfdp_transaction_store_t;

extern cfdp_task_memory_t cfdp_mem;

extern cfdp_transaction_store_t cfdp_txn_store;

extern cfdp_large_buff_t cfdp_large_buff;
extern cfdp_small_buff_t cfdp_small_buffs[CFDP_SMALL_BUFF_COUNT];

QueueHandle_t init_cfdp(void);
void main_cfdp(void *pvParameters);

cfdp_result_t cfdp_handle_send_state(cfdp_transaction_t *transaction);
cfdp_result_t cfdp_handle_recv_state(cfdp_transaction_t *transaction);
cfdp_result_t cfdp_transact(cfdp_transaction_t *txn);

void cfdp_put_request(const cfdp_put_t *put);
// Finds where a put's file is in MRAM and how big it is. SUCCESS only if there's a file to send
status_t cfdp_find_file(const cfdp_put_t *put, mram_region_t *p_region, uint32_t *p_offset, uint32_t *p_size);
// A put of `file` (a perovskite sweep's slot in `index`) on `stream`, for enqueue_command()
command_t get_cfdp_put_command(cfdp_file_t file, uint8_t index, radio_stream_t stream);
// Called when a send transaction completes. If the ground received the whole file, clears the state machine input
// saying it's waiting for downlink
void cfdp_send_complete(const cfdp_transaction_t *txn);
void cfdp_cancel_request(void);
void cfdp_cancel(void);

int cfdp_prepare_pdu_header(uint8_t *buff, cfdp_transaction_t *transaction, uint16_t pdu_len, cfdp_pdu_type_t pdu_type);
int cfdp_send_metadata(cfdp_transaction_t *transaction);
int cfdp_send_filedata(cfdp_transaction_t *transaction, uint32_t offset, uint32_t size);

int cfdp_send_fin(cfdp_transaction_t *transaction);
int cfdp_send_reject_fin(const cfdp_pdu_header_t *header, const cfdp_pdu_metadata_t *meta, uint8_t condition_code);
int cfdp_send_ack(cfdp_transaction_t *transaction, uint8_t acked_directive_code, uint8_t directive_subtype_code, uint8_t condition_code,
                  uint8_t transaction_status);
int cfdp_send_stale_ack(const cfdp_pdu_header_t *header, uint8_t condition_code);
int cfdp_send_eof(cfdp_transaction_t *transaction);
int cfdp_send_nak(cfdp_transaction_t *transaction);
int cfdp_send_metadata_nak(cfdp_pdu_header_t *header);
int cfdp_resend(cfdp_transaction_t *transaction);

void exec_command_cfdp_request(command_t *const p_cmd);
void cfdp_process_pdu(uint8_t *raw, size_t sz, uint8_t vcid);

/* Defined elsewhere for simulation purposes, but not used in the sim.

QueueHandle_t init_cfdp(void);
void main_cfdp(void* pvParameters);

*/

#endif // CFDP_TASK_H
