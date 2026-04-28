#ifndef RADIO_CFDP_ENGN
#define RADIO_CFDP_ENGN

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cfdp_pdu.h"
#include "drivers/at86rf215/at86rf215.h"

typedef struct at86rf215 at86rf215_t;

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

#define TX_TIMEOUT_MS 5000UL // max time to wait for a single PDU transmission to complete

// DUMMY FUNCTIONS
uint32_t next_seq_num(void);

void uint32_to_big_endian(uint32_t src, uint8_t dst[4]);
void uint16_to_big_endian(uint16_t src, uint8_t dst[2]);

int cfdp_nak_push(cfdp_nak_buf_t buf, cfdp_pdu_segment_request_t segment);
cfdp_pdu_segment_request_t cfdp_nak_buf_pop(cfdp_nak_buf_t *buf);

cfdp_transaction_t *cfdp_alloc_transaction(cfdp_transaction_store_t *txn_store);
cfdp_transaction_t *cfdp_find_transaction(cfdp_transaction_store_t *txn_store, uint32_t entity_id, uint32_t seq_num);

void cfdp_send_prompt(cfdp_transaction_t *txn);

cfdp_transaction_t *cfdp_send_init(cfdp_transaction_store_t *txn_store, uint8_t *fl, uint32_t sz, cfdp_lv_t source_filename,
                                   cfdp_lv_t dest_filename, uint32_t source_entity_id, uint32_t dest_entity_id, uint8_t channel_num,
                                   uint8_t priority, bool reliable_mode, at86rf215_t *radio_handle);

void cfdp_handle_send_state(cfdp_transaction_t *transaction, uint32_t elapsed_ms);
void cfdp_handle_recv_state(cfdp_transaction_t *transaction, uint32_t elapsed_ms);

int cfdp_prepare_pdu_header(uint8_t *buff, cfdp_transaction_t *transaction, uint16_t pdu_len, cfdp_pdu_type_t pdu_type);

int cfdp_send_metadata(cfdp_transaction_t *transaction);

int cfdp_send_filedata(cfdp_transaction_t *transaction, uint32_t offset, uint32_t size);

uint32_t cfdp_calculate_modular_checksum(cfdp_transaction_t *transaction);

int cfdp_send_eof(cfdp_transaction_t *transaction, uint8_t condition_code);

int cfdp_resend(cfdp_transaction_t *transaction);

int cfdp_send_init_simple(uint8_t *fl, size_t sz, struct at86rf215 *radio_handle, cfdp_transaction_store_t *txn_store);

cfdp_result_t cfdp_transact(cfdp_transaction_t *txn, uint32_t elapsed_ms);

/**
 * CFDP TODOs
 *
 * TODO(done): PDU parsing/building foundation.
 * TODO(done): Implement and validate the transaction loop/state progression.
 * TODO(open): Finalize active transaction storage strategy (queue/store ownership and lifecycle).
 * TODO(open): Decide MRAM persistence policy for transaction sequence number and related state.
 * TODO(open): Define file-data PDU segmentation policy (full file vs fixed segments/chunks).
 * TODO(open): Implement both modular and null checksum modes for Blue Book compliance.
 * TODO(optional): Implement CRC algorithm
 */
#endif