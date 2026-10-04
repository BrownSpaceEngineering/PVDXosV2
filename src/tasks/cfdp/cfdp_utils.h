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

static inline void read_cam_mem(void *dest, size_t sz) {
    (void)dest;
    (void)sz;
    return;
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

int cfdp_send(void *buff, size_t sz);
int cfdp_recv(void *buff, size_t sz);

void cfdp_nak_buf_push(cfdp_nak_buf_t *buf, cfdp_pdu_segment_request_t segment);
cfdp_pdu_segment_request_t cfdp_nak_buf_pop(cfdp_nak_buf_t *buf);
size_t cfdp_nak_buf_get_index(cfdp_nak_buf_t *buf, size_t offset, size_t len);

void fin_txn_buf_push(fin_txn_buf_t *buf, fin_txn_t txn);
fin_txn_t fin_txn_buf_pop(fin_txn_buf_t *buf);

cfdp_transaction_t *cfdp_alloc_transaction(cfdp_transaction_store_t *txn_store);
void cfdp_free_transaction(cfdp_transaction_store_t *txn_store, cfdp_transaction_t *txn);
cfdp_transaction_t *cfdp_find_transaction(cfdp_transaction_store_t *txn_store, uint32_t entity_id, uint32_t seq_num);

uint8_t *cfdp_alloc_small_buff();
uint8_t *cfdp_alloc_large_buff();
int cfdp_free_buff(uint8_t *buff);

uint32_t cfdp_calculate_modular_checksum(cfdp_transaction_t *transaction);

#endif
