#ifndef CFDP_UTILS_H
#define CFDP_UTILS_H

#include "cfdp_task.h"

#define CFDP_SAVED_TXNS 8

// SPP APID for CFDP packets - TODO: replace with actual SPP APID
#define CFDP_APID 0x02

#ifdef UNITTEST
extern uint8_t test_mem[512];
#endif

static inline void uint32_to_big_endian(uint32_t src, uint8_t dst[4]) {
    dst[0] = (src >> 24) & 0xFF;
    dst[1] = (src >> 16) & 0xFF;
    dst[2] = (src >> 8) & 0xFF;
    dst[3] = src & 0xFF;
}

static inline void uint16_to_big_endian(uint16_t src, uint8_t dst[2]) {
    dst[0] = (src >> 8) & 0xFF;
    dst[1] = src & 0xFF;
}

typedef struct {
    uint32_t src_entity_id;
    uint32_t seq_num;
} fin_txn_t;

typedef struct {
    fin_txn_t txns[CFDP_SAVED_TXNS];
    uint32_t tail;
    uint32_t head;
    uint32_t size;
} fin_txn_buf_t;

extern fin_txn_buf_t cfdp_fin_txn;

uint32_t next_seq_num(void);

// How long cfdp_send waits for room in the downlink queue. File PDUs don't wait, since the state machine retries them
// on the next pass; replies (ACK/NAK/FIN) are usually sent once from a PDU handler, so they wait briefly
#define CFDP_SEND_NO_WAIT 0
#define CFDP_SEND_REPLY_WAIT pdMS_TO_TICKS(100)

int cfdp_send(void *buff, size_t sz, radio_stream_t stream);
int cfdp_recv(void *buff, size_t sz, uint8_t *vcid);

void cfdp_nak_buf_push(cfdp_nak_buf_t *buf, cfdp_pdu_segment_request_t segment);
cfdp_pdu_segment_request_t cfdp_nak_buf_pop(cfdp_nak_buf_t *buf);
size_t cfdp_nak_buf_get_index(cfdp_nak_buf_t *buf, size_t offset, size_t len);

void fin_txn_buf_push(fin_txn_buf_t *buf, fin_txn_t txn);
fin_txn_t fin_txn_buf_pop(fin_txn_buf_t *buf);

cfdp_transaction_t *cfdp_alloc_transaction(cfdp_transaction_store_t *txn_store);
void cfdp_free_transaction(cfdp_transaction_store_t *txn_store, cfdp_transaction_t *txn);
cfdp_transaction_t *cfdp_find_transaction(cfdp_transaction_store_t *txn_store, uint32_t entity_id, uint32_t seq_num);

cfdp_buff_header_t *cfdp_alloc_small_buff();
cfdp_buff_header_t *cfdp_alloc_large_buff();
int cfdp_free_buff(cfdp_buff_header_t *buff);
int cfdp_release_buff(cfdp_buff_header_t *buff);
int cfdp_add_buff_reference(cfdp_buff_header_t *buff);

uint32_t cfdp_calculate_modular_checksum(cfdp_transaction_t *transaction);

#endif
