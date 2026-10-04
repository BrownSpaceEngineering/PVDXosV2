#include "cfdp_utils.h"

#include <string.h>

#include "cfdp_task.h"

uint32_t next_seq_num(void) {
    static uint32_t counter = 0;
    return ++counter;
}

int cfdp_send(void *buff, size_t sz) {
#if defined(UNITTEST)
    memcpy(test_mem, buff, sz);
#else
    (void)buff;
    (void)sz;
#endif
    return 0;
}

int cfdp_recv(void *buff, size_t sz) {
    (void)buff;
    (void)sz;
    return 0;
}

void cfdp_nak_buf_push(cfdp_nak_buf_t *buf, cfdp_pdu_segment_request_t segment) {
    if (buf->size == 0) {
        buf->size = 1;
        buf->tail = 0;
        buf->head = 0;
        buf->segments[0] = segment;
        return;
    }

    if (buf->size == CFDP_MAX_SEGMENT_REQUESTS) {
        buf->head = (buf->head + 1) % CFDP_MAX_SEGMENT_REQUESTS;
        buf->tail = (buf->tail + 1) % CFDP_MAX_SEGMENT_REQUESTS;
        buf->segments[buf->head] = segment;
        return;
    }

    buf->head = (buf->head + 1) % CFDP_MAX_SEGMENT_REQUESTS;
    buf->segments[buf->head] = segment;
    buf->size += 1;
}

cfdp_pdu_segment_request_t cfdp_nak_buf_pop(cfdp_nak_buf_t *buf) {
    if (buf->size == 0) {
        return (cfdp_pdu_segment_request_t){.start_offset = ((uint32_t)-1), .end_offset = ((uint32_t)-1)};
    }

    cfdp_pdu_segment_request_t seg = buf->segments[buf->tail];
    buf->tail = (buf->tail + 1) % CFDP_MAX_SEGMENT_REQUESTS;
    buf->size -= 1;
    return seg;
}

size_t cfdp_nak_buf_get_index(cfdp_nak_buf_t *buf, size_t offset, size_t len) {
    for (size_t i = 0; i < buf->size; ++i) {
        size_t idx = (buf->tail + i) % CFDP_MAX_SEGMENT_REQUESTS;
        if (buf->segments[idx].start_offset < offset + len && buf->segments[idx].end_offset > offset) {
            return idx;
        }
    }
    return CFDP_MAX_SEGMENT_REQUESTS;
}

void fin_txn_buf_push(fin_txn_buf_t *buf, fin_txn_t txn) {
    if (buf->size == 0) {
        buf->size = 1;
        buf->tail = 0;
        buf->head = 0;
        buf->txns[0] = txn;
        return;
    }

    if (buf->size == CFDP_SAVED_TXNS) {
        buf->head = (buf->head + 1) % CFDP_SAVED_TXNS;
        buf->tail = (buf->tail + 1) % CFDP_SAVED_TXNS;
        buf->txns[buf->head] = txn;
        return;
    }

    buf->head = (buf->head + 1) % CFDP_SAVED_TXNS;
    buf->txns[buf->head] = txn;
    buf->size += 1;
}

fin_txn_t fin_txn_buf_pop(fin_txn_buf_t *buf) {
    if (buf->size == 0) {
        return (fin_txn_t){.seq_num = ((uint32_t)-1), .src_entity_id = ((uint32_t)-1)};
    }

    fin_txn_t txn = buf->txns[buf->tail];
    buf->tail = (buf->tail + 1) % CFDP_SAVED_TXNS;
    buf->size -= 1;
    return txn;
}

cfdp_transaction_t *cfdp_alloc_transaction(cfdp_transaction_store_t *txn_store) {
    if (!txn_store->slot_free) {
        return NULL;
    }
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        if (!txn_store->active[i]) {
            txn_store->active[i] = true;
            memset(&txn_store->transactions[i], 0, sizeof(cfdp_transaction_t));

            // Attach the pre-created static timer handles for this slot.
            // Handles were created once in init_cfdp(); the timer IDs are set
            // to the transaction pointer by the caller after full initialisation.
            txn_store->transactions[i].inactivity_timer_handle = cfdp_mem.inactivity_timer_handles[i];
            txn_store->transactions[i].retransmit_timer_handle = cfdp_mem.retransmit_timer_handles[i];

            txn_store->slot_free = false;
            for (int j = 0; j < MAX_TRANSACTIONS; j++) {
                if (!txn_store->active[j]) {
                    txn_store->slot_free = true;
                    break;
                }
            }
            return &txn_store->transactions[i];
        }
    }
    txn_store->slot_free = false;
    warning("cfdp: mismatch between slot_free and txn store capacity");
    return NULL;
}

void cfdp_free_transaction(cfdp_transaction_store_t *txn_store, cfdp_transaction_t *txn) {
    if (txn_store == NULL || txn == NULL) {
        return;
    }
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        if (&txn_store->transactions[i] == txn) {
            txn_store->active[i] = false;
            txn_store->slot_free = true;
            return;
        }
    }
}

cfdp_transaction_t *cfdp_find_transaction(cfdp_transaction_store_t *txn_store, uint32_t entity_id, uint32_t seq_num) {
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        if (txn_store->active[i] && txn_store->transactions[i].transaction_id.entity_id == entity_id &&
            txn_store->transactions[i].transaction_id.seq_num == seq_num) {
            return &txn_store->transactions[i];
        }
    }
    return NULL;
}

uint8_t *cfdp_alloc_small_buff() {
    for (size_t i = 0; i < CFDP_SMALL_BUFF_COUNT; ++i) {
        if (!cfdp_small_buffs.in_use[i]) {
            cfdp_small_buffs.in_use[i] = true;
            return &cfdp_small_buffs.buff[i * CFDP_SMALL_BUFF_SZ];
        }
    }
    return NULL;
}

uint8_t *cfdp_alloc_large_buff() {
    if (cfdp_large_buff.in_use) {
        return NULL;
    } else {
        cfdp_large_buff.in_use = true;
        return cfdp_large_buff.buff;
    }
}

int cfdp_free_buff(uint8_t *buff) {
    if (cfdp_large_buff.buff == buff) {
        cfdp_large_buff.in_use = false;
        return 0;
    }
    for (size_t i = 0; i < CFDP_SMALL_BUFF_COUNT; ++i) {
        if (&cfdp_small_buffs.buff[i * CFDP_SMALL_BUFF_SZ] == buff) {
            cfdp_small_buffs.in_use[i] = false;
            return 0;
        }
    }
    return -1;
}

uint32_t cfdp_calculate_modular_checksum(cfdp_transaction_t *txn) {
    size_t words = txn->file_size / 4;
    uint32_t checksum = 0;

    for (uint32_t i = 0; i < words * 4; i += 4) {
        checksum += (((uint32_t)txn->file_data[i] << 24) | ((uint32_t)txn->file_data[i + 1] << 16) |
                     ((uint32_t)txn->file_data[i + 2] << 8) | txn->file_data[i + 3]);
    }

    size_t rem = txn->file_size % 4;

    uint32_t checksum_rem = 0;
    for (uint32_t i = 0; i < rem; i++) {
        checksum_rem |= (uint32_t)txn->file_data[words * 4 + i] << (8 * (3 - i));
    }

    return checksum + checksum_rem;
}
