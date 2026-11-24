#include "cfdp_utils.h"

#include <string.h>

#include "ccsds/radio_routing.h"
#include "cfdp_task.h"
#include "cfdp_timer.h"
#include "globals.h"
#include "logging.h"
#include "rtos_start.h"
#include "uhf_radio/uhf_radio_task.h"

uint32_t next_seq_num(void) {
    static uint32_t counter = 0;
    return ++counter;
}

int cfdp_send(void *buff, size_t sz, radio_stream_t stream) {
#if defined(UNITTEST)
    // Tests run before the scheduler starts and without the radio's queues, so they just capture the PDU
    memcpy(test_mem, buff, sz);
    (void)stream;
    return 0;
#endif
    if (sz == 0 || sz > RADIO_PAYLOAD_MAX_SIZE) {
        return -1; // PDU doesn't fit
    }

    // The radio task wraps the PDU in an SPP packet when it sends it
    radio_queue_item_t item;
    item.len = (uint16_t)sz;
    item.apid = CCSDS_APID_CFDP;
    memcpy(item.data, buff, sz);

    TickType_t wait = (stream == RADIO_STREAM_CFDP_REPLY) ? CFDP_SEND_REPLY_WAIT : CFDP_SEND_NO_WAIT;
    if (radio_route_send(stream, &item, wait)) {
        return -1; // queue full, caller retries later
    }

    return 0;
}

int cfdp_recv(void *buff, size_t sz, uint8_t *vcid) {
    if (sz < sizeof(radio_queue_item_t)) {
        return -1; // buff is too small to recieve radio_queue_item_t
    }

    if (xQueueReceive(uplink_queue, buff, 0) != pdPASS) {
        return 0; // queue empty, nothing to recieve.
    }

    // The radio task has already checked the SPP headers and stripped them, so data is the PDU
    radio_queue_item_t *item = (radio_queue_item_t *)buff;
    *vcid = item->vcid;

    if (item->apid != CCSDS_APID_CFDP) {
        return -1; // not a CFDP packet
    }

    size_t len = item->len;
    if (len == 0 || len > sizeof(item->data)) {
        return -1; // no PDU, and 0 is reserved for an empty queue
    }

    // item->data is inside buff, so the two overlap
    memmove(buff, item->data, len);

    return (int)len;
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

// Only the CFDP task calls these
cfdp_transaction_t *cfdp_alloc_transaction(void) {
    if (cfdp_txn_store.active) {
        return NULL;
    }

    // The previous transaction's timers were stopped when it was freed, but one may have fired just before, so drop
    // any timeout or cancel still pending for it
    xTaskNotifyWait(0, CFDP_NOTIFY_ALL, NULL, 0);

    cfdp_txn_store.active = true;
    memset(&cfdp_txn_store.txn, 0, sizeof(cfdp_txn_store.txn));
    cfdp_txn_store.txn.pass = radio_link_view().pass;
    return &cfdp_txn_store.txn;
}

void cfdp_free_transaction(void) {
    if (!cfdp_txn_store.active) {
        return;
    }
    cfdp_transaction_t *txn = &cfdp_txn_store.txn;

    // The timer outlives the transaction, so stop it or it fires on the next one
    stop_timer(cfdp_mem.inactivity_timer_handle);

    // Received files are in cfdp_file_buff, which is free again for the next transaction
    txn->file_data = NULL;
    cfdp_txn_store.active = false;
}

cfdp_transaction_t *cfdp_find_transaction(uint32_t entity_id, uint32_t seq_num) {
    cfdp_transaction_t *txn = &cfdp_txn_store.txn;
    if (cfdp_txn_store.active && txn->transaction_id.entity_id == entity_id && txn->transaction_id.seq_num == seq_num) {
        return txn;
    }
    return NULL;
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

uint32_t cfdp_checksum_add(uint32_t checksum, uint32_t offset, const uint8_t *data, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        checksum += (uint32_t)data[i] << (8 * (3 - ((offset + i) % 4)));
    }
    return checksum;
}

bool cfdp_read_file(const cfdp_transaction_t *txn, uint32_t offset, uint8_t *dst, uint32_t size) {
    if (offset > txn->file_size || size > txn->file_size - offset) {
        return false;
    }
    if (txn->file_data != NULL) {
        memcpy(dst, txn->file_data + offset, size);
        return true;
    }
#ifdef NO_MRAM
    return false; // A NO_MRAM build has nowhere to read a file from
#else
    return mram_region_read(txn->file_mram_region, txn->file_mram_offset + offset, dst, size);
#endif
}

#define CFDP_CHECKSUM_CHUNK_SZ 256 // Bytes read at a time by cfdp_file_checksum, on the CFDP task's stack

bool cfdp_file_checksum(const cfdp_transaction_t *txn, uint32_t size, uint32_t *p_checksum) {
    uint8_t chunk[CFDP_CHECKSUM_CHUNK_SZ];
    uint32_t checksum = 0;
    for (uint32_t offset = 0; offset < size;) {
        uint32_t n = (size - offset > sizeof(chunk)) ? sizeof(chunk) : size - offset;
        if (!cfdp_read_file(txn, offset, chunk, n)) {
            return false;
        }
        checksum = cfdp_checksum_add(checksum, offset, chunk, n);
        offset += n;
    }
    *p_checksum = checksum;
    return true;
}
