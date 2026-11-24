#ifndef CFDP_UTILS_H
#define CFDP_UTILS_H

#include "cfdp_task.h"

#define CFDP_SAVED_TXNS 8

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

cfdp_transaction_t *cfdp_alloc_transaction(void);
void cfdp_free_transaction(void);
cfdp_transaction_t *cfdp_find_transaction(uint32_t entity_id, uint32_t seq_num);

uint32_t cfdp_calculate_modular_checksum(cfdp_transaction_t *transaction);

// Adds `size` bytes of file data, starting at file offset `offset`, to a modular checksum. Any offset works, since
// each byte is placed by its own offset in the file's 4-byte words
uint32_t cfdp_checksum_add(uint32_t checksum, uint32_t offset, const uint8_t *data, uint32_t size);
// Reads `size` bytes of a transaction's file from `offset`, from RAM or MRAM (see cfdp_transaction_t.file_data). False if
// the range isn't within the file or the MRAM read fails
bool cfdp_read_file(const cfdp_transaction_t *txn, uint32_t offset, uint8_t *dst, uint32_t size);
// Modular checksum of a transaction's file's first `size` bytes, read in pieces with cfdp_read_file()
bool cfdp_file_checksum(const cfdp_transaction_t *txn, uint32_t size, uint32_t *p_checksum);

#endif
