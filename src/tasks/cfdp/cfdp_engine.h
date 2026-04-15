#ifndef RADIO_CFDP_ENGN
#define RADIO_CFDP_ENGN

#include "cfdp_pdu.h"

#define MAX_FILE_SIZE 4096 // placeholder
#define SEGMENT_SIZE 32    // placeholder

#define REQ_CLOSURE 0 // 1 : requested, 0 : not requested
#define CHECKSUM_TYPE 0

// DUMMY FUNCTIONS
void send(uint8_t *buff, size_t sz) {
    return;
}

uint32_t next_seq_num() {
    return 0;
}

typedef enum cfdp_state {
    CFPD_SEND_STATE_METADATA_SEND = 0,
    CFPD_SEND_STATE_FILE_SEND,
    CFDP_SEND_STATE_WAIT_ACK,
    CFDP_SEND_STATE_WAIT_FIN,
    CFDP_SEND_STATE_DONE,
    CFDP_SEND_STATE_ERR,

    CFDP_RECV_STATE_FILE_RECV, // need more?
    CFDP_RECV_STATE_SEND_NAK,
    CFDP_RECV_STATE_DONE,
    CFDO_RECV_STATE_ERR
} cfdp_state_t;

typedef enum cfdp_direction {
    CFDP_SEND = 0,
    CFDP_RECV
} cfdp_direction_t;

typedef enum cfdp_pdu_type {
    CFDP_FILE_DIRECTIVE = 0,
    CFDP_FILE_DATA
} cfdp_pdu_type_t;

// used by sender to track segments that the reciever may have NAKed
// used by recieve to track segments not recieved, will be NAKed if in Class II
typedef struct cfdp_gap_tracker {
    uint32_t bitmap[MAX_FILE_SIZE / SEGMENT_SIZE];
    uint32_t file_size;
    uint32_t bytes_recv;
    bool eof_recv; // will we check this here or elsewhere?
} cfdp_gap_tracker_t;

typedef struct cfdp_transaction {
    cfdp_transaction_id_t *transaction_id;
    uint32_t dest_entity_id;

    uint32_t inactivity_timer;
    uint32_t ack_timer; // in NASA implementation also NAK timer... need to look into this.

    cfdp_gap_tracker_t *gaps;

    uint32_t file_size;
    uint32_t file_offset;

    cfdp_state_t state;
    cfdp_pdu_direction_t direction;

    bool reliable_mode;

    uint8_t channel_num;
    uint8_t priority;

    uint8_t file_data;

    cfdp_lv_t source_filename;
    cfdp_lv_t dest_filename;

} cfdp_transaction_t;

// must have sufficient space in the dst buffer (NOT MEMORY SAFE)
void uint32_to_big_endian(uint32_t src, uint8_t *dst);
void uint16_to_big_endian(uint16_t src, uint8_t *dst);

int cfdp_send_init(uint8_t *fl, uint32_t sz, cfdp_lv_t source_filename, cfdp_lv_t dest_filename, uint32_t source_entity_id,
                   uint32_t dest_entity_id, uint8_t channel_num, uint8_t priority, bool reliable_mode);

void cfdp_handle_send_state(cfdp_transaction_t *transaction);

int cfdp_prepare_pdu_header(uint8_t *buff, cfdp_transaction_t *transaction, uint16_t pdu_len, cfdp_pdu_type_t pdu_type);

int cfdp_send_metadata(cfdp_transaction_t *transaction);

int cfdp_send_filedata(cfdp_transaction_t *transaction, uint32_t offset);

int cfdp_send_next_filedata(cfdp_transaction_t *transaction);

int cfdp_send_eof(cfdp_transaction_t *transaction, uint8_t condition_code);

int cfdp_resend(cfdp_transaction_t *transaction);

/**
 * CFDP IMPLEMENTATION OUTLINE
 * - PDU's --> Done
 * - Transaction loop
 *
 * QUESTIONS TO ASK
 * - Queues? How to store active transactions
 * - How flexible are we in MRAM (is anything in the data seg going to be backed up)? --> need to store global seq #
 * - How much of the file do we send in each filedata PDU? All of it?
 **/
#endif