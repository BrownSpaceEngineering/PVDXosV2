#include "cfdp_engine.h"

#include "cfdp_pdu.h"
#include "cfdp_task.h"
#include "string.h"
/*""
    "TODOS:
    1. Add in timer logic

    2. Ensure all macro numbering is proper

    3. Transact function that handles everything in a single flow

    4. ""

    "*/

// note : this currently doesn't build but i dont have time to fix it rn...

/**
 * cfdp_send_init
 *
 * \brief Allocates a transaction slot from txn_store and initialises it for sending.
 *
 * \param txn_store, the transaction store to allocate from
 * \param fl, an array of uint8_t representing the bytes of a file
 * \param sz, the size of the file in bytes
 * \param source_filename, the representation of file on this device
 * \param dest_filename, the representation of file on receiving device
 * \param source_entity_id, the entity ID of this device
 * \param dest_entity_id, the entity ID of the destination device
 * \param channel_num, the channel this transaction will take place over
 * \param priority, this transaction's priority
 * \param reliable_mode, whether this transaction should occur in reliable mode
 * \param radio_handle, pointer to the initialised AT86RF215 radio driver instance
 *
 * \return pointer to the allocated transaction on success, NULL if args are invalid or no free slot
 */
cfdp_transaction_t *cfdp_send_init(cfdp_transaction_store_t *txn_store, uint8_t *fl, uint32_t sz, cfdp_lv_t source_filename,
                                   cfdp_lv_t dest_filename, uint32_t source_entity_id, uint32_t dest_entity_id, uint8_t channel_num,
                                   uint8_t priority, bool reliable_mode, at86rf215_t *radio_handle) {
    if (txn_store == NULL || fl == NULL || sz == 0 || radio_handle == NULL) {
        return NULL;
    }

    cfdp_transaction_t *txn = cfdp_alloc_transaction(txn_store);
    if (txn == NULL) {
        return NULL; // no free slots
    }

    txn->transaction_id.entity_id = source_entity_id;
    txn->transaction_id.seq_num = next_seq_num();
    txn->dest_entity_id = dest_entity_id;
    txn->inactivity_timer = 0;
    txn->ack_timer = 0;
    txn->nak_timer = 0;
    txn->eof_retransmit_counter = 0;
    txn->nak_retransmit_counter = 0;
    txn->nak_buf.head = 0;
    txn->nak_buf.tail = 0;
    txn->nak_buf.size = 0;
    txn->file_size = sz;
    txn->file_offset = 0;
    txn->state = CFDP_SEND_STATE_METADATA_SEND;
    txn->direction = CFDP_SEND;
    txn->reliable_mode = reliable_mode;
    txn->channel_num = channel_num;
    txn->priority = priority;
    txn->file_data = fl;
    txn->source_filename = source_filename;
    txn->dest_filename = dest_filename;
    txn->radio_handle = radio_handle;

    return txn;
}

int cfdp_send_init_simple(uint8_t *fl, size_t sz, at86rf215_t *radio_handle, cfdp_transaction_store_t *txn_store) {
    cfdp_lv_t empty = {.length = 0, .value = NULL};
    return cfdp_send_init(txn_store, fl, sz,
                          empty,                // source_file
                          empty,                // dest_file
                          ENTITY_ID_SPACECRAFT, // source_entity_id
                          ENTITY_ID_GROUND,     // dest_entity_id
                          0,                    // channel_num
                          0,                    // priority
                          true,                 // reliable_mode
                          radio_handle          // radio handle
                          ) != NULL
        ? 0
        : -1;
}

void cfdp_handle_send_state(cfdp_transaction_t *transaction, uint32_t elapsed_ms) {
    switch (transaction->state) {
        case CFDP_SEND_STATE_METADATA_SEND:
            cfdp_send_metadata(transaction);
            transaction->state = CFDP_SEND_STATE_FILE_SEND;
            break;
        case CFDP_SEND_STATE_FILE_SEND:
            if (transaction->nak_buf.size > 0) {
                cfdp_resend(transaction);
            } else if (transaction->file_offset < transaction->file_size) {
                uint32_t remaining = transaction->file_size - transaction->file_offset;
                uint32_t chunk_size = (remaining < SEGMENT_SIZE) ? remaining : SEGMENT_SIZE;
                cfdp_send_filedata(transaction, transaction->file_offset, chunk_size);
            } else {
                cfdp_send_eof(transaction, CFDP_COND_NOERROR);
                transaction->state = transaction->reliable_mode ? CFDP_SEND_STATE_WAIT_FIN : CFDP_SEND_STATE_DONE;
            }
            break;
        case CFDP_SEND_STATE_WAIT_ACK:
            transaction->ack_timer += elapsed_ms; // update timer waiting for ACK
            if (transaction->ack_timer > ACK_TIMEOUT_MS) {
                cfdp_send_eof(transaction, CFDP_COND_NOERROR);
                transaction->ack_timer = 0;
            }
            break;
        case CFDP_SEND_STATE_WAIT_FIN:
            if (transaction->nak_buf.size > 0) {
                cfdp_resend(transaction);
            }
            break;
        case CFDP_SEND_STATE_ERR:
            // panic? or just fail silently?
            break;
        default:
            // panic! A send transaction should always be one of these!
    }
}

/**
 * cfdp_send
 *
 * Transmits a fully-assembled PDU buffer over the radio bound to this transaction.
 * Uses the transaction's radio_handle and channel_num so callers don't repeat the
 * at86rf215 argument pattern everywhere.
 */
static void cfdp_send(cfdp_transaction_t *transaction, const uint8_t *buff, size_t sz) {
    at86rf215_tx_frame(transaction->radio_handle, (at86rf215_radio_t)transaction->channel_num, buff, sz, TX_TIMEOUT_MS);
}

/**
 * cfdp_send_nak
 *
 * Builds and transmits a NAK PDU listing every segment gap currently in the
 * transaction's nak_buf. Does NOT pop the buffer — gaps stay tracked until the
 * sender retransmits and the caller clears them on receipt.
 */
static void cfdp_send_nak(cfdp_transaction_t *transaction) {
    if (transaction == NULL || transaction->nak_buf.size == 0) {
        return;
    }

    uint32_t n = transaction->nak_buf.size;
    // NAK payload: 1 (directive code) + 4 (start_of_scope) + 4 (end_of_scope) + 8*n (segment requests)
    size_t nak_data_size = 9 + (8 * n);
    uint8_t buff[16 + nak_data_size];

    cfdp_prepare_pdu_header(buff, transaction, (uint16_t)nak_data_size, CFDP_FILE_DIRECTIVE);

    uint8_t *nak_buff = buff + 16;
    nak_buff[0] = CFDP_DIR_NAK;                                 // directive code
    uint32_to_big_endian(0, nak_buff + 1);                      // start_of_scope: beginning of file
    uint32_to_big_endian(transaction->file_size, nak_buff + 5); // end_of_scope: full file extent

    // Iterate the ring buffer without popping — read segments in tail→head order
    for (uint32_t i = 0; i < n; i++) {
        cfdp_pdu_segment_request_t seg = transaction->nak_buf.segments[(transaction->nak_buf.tail + i) % CFDP_MAX_SEGMENT_REQUESTS];
        uint32_to_big_endian(seg.start_offset, nak_buff + 9 + (i * 8));
        uint32_to_big_endian(seg.end_offset, nak_buff + 13 + (i * 8));
    }

    cfdp_send(transaction, buff, 16 + nak_data_size);
}

/**
 * cfdp_handle_recv_state
 *
 * One tick of the receive-side state machine. Called once per cfdp_transact
 * invocation when direction == CFDP_RECV.
 *
 * Incoming PDUs (file data, EOF) are fed to the transaction from outside via
 * cfdp_recv_pdu (not yet implemented) which updates nak_buf and file_offset.
 * This function handles the timer-driven NAK retry logic.
 */
void cfdp_handle_recv_state(cfdp_transaction_t *transaction, uint32_t elapsed_ms) {
    switch (transaction->state) {
        case CFDP_RECV_STATE_FILE_RECV:
            // do anything?
            break;
        case CFDP_RECV_STATE_SEND_NAK:
            cfdp_send_nak(transaction);
            transaction->state = CFDP_RECV_STATE_WAIT_ACK;
            transaction->ack_timer = 0;
            break;
        case CFDP_RECV_STATE_WAIT_ACK:
            transaction->ack_timer += elapsed_ms;
            if (transaction->ack_timer > ACK_TIMEOUT_MS) {
                transaction->state = CFDP_RECV_STATE_SEND_NAK;
            }
            break;
        case CFDP_RECV_STATE_SEND_FIN:
            transaction->ack_timer += elapsed_ms; // update timer waiting for ACK
            if (transaction->ack_timer > ACK_TIMEOUT_MS) {
                // cfdp_send_fin(transaction); need to implement
                transaction->ack_timer = 0;
            }
            break;
        case CFDP_RECV_STATE_ERR:
        default:
    }
}

/**
 *List of bit conversion functions
 *
 * */
void uint32_to_big_endian(uint32_t src, uint8_t dst[4]) {
    dst[0] = (src >> 24) & 0xFF;
    dst[1] = (src >> 16) & 0xFF;
    dst[2] = (src >> 8) & 0xFF;
    dst[3] = src & 0xFF;
}

void uint16_to_big_endian(uint16_t src, uint8_t dst[2]) {
    dst[0] = (src >> 8) & 0xFF;
    dst[1] = src & 0xFF;
}

/**
 * Functions for interfacing with the NAK Buffer
 */
void cfdp_nak_buf_push(cfdp_nak_buf_t *buf, cfdp_pdu_segment_request_t segment) {
    if (buf->size == 0) {
        buf->size = 1;
        buf->tail = 0;
        buf->head = 0;
        buf->segments[0] = segment;
        return;
    }

    // overwrites oldest data
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

cfdp_transaction_t *cfdp_alloc_transaction(cfdp_transaction_store_t *txn_store) {
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        if (!txn_store->active[i]) {
            txn_store->active[i] = true;
            memset(&txn_store->transactions[i], 0, sizeof(cfdp_transaction_t));
            return &txn_store->transactions[i];
        }
    }
    return NULL; // no free slots
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

/**
 *
 *
 */
int cfdp_prepare_pdu_header(uint8_t *buff, cfdp_transaction_t *transaction, uint16_t pdu_len, cfdp_pdu_type_t pdu_type) {
    if (buff == NULL || transaction == NULL || pdu_len == 0)
        return -1;

    uint8_t direction = (transaction->direction == CFDP_SEND) ? 1 : 0;
    uint8_t mode = (transaction->reliable_mode) ? 0 : 1;
    uint8_t crc_present = 0; // does USLP handle this?
    uint8_t large_file = 0;

    buff[0] = (0b001 << 5) | ((pdu_type & 0b1) << 4) | (direction << 3) | (mode << 2) | (crc_present << 1) | large_file;

    uint16_to_big_endian(pdu_len, buff + 1);

    uint8_t has_segment_metadata = 0;
    buff[3] = 0b00110011 | (has_segment_metadata << 3); // sets both entity ID/trans sequence length to 3 (4 octets)

    uint32_to_big_endian(transaction->transaction_id.entity_id, buff + 4);

    uint32_to_big_endian(transaction->transaction_id.seq_num, buff + 8);

    uint32_to_big_endian(transaction->dest_entity_id, buff + 12);

    return 0;
}

int cfdp_send_metadata(cfdp_transaction_t *transaction) {
    if (transaction == NULL)
        return 0;

    size_t source_filename_length = transaction->source_filename.length;
    size_t dest_filename_length = transaction->dest_filename.length;
    size_t metadata_size = 7 + source_filename_length + dest_filename_length;

    uint8_t buff[metadata_size + 16];
    cfdp_prepare_pdu_header(buff, transaction, metadata_size & 0xFFFF, CFDP_FILE_DIRECTIVE);

    uint8_t *metadata_buff = buff + 16;

    metadata_buff[0] = 0;

    uint8_t req_closure = (transaction->reliable_mode) ? 0 : REQ_CLOSURE;
    metadata_buff[0] |= (req_closure << 6);
    metadata_buff[0] |= CHECKSUM_TYPE;

    uint32_to_big_endian(transaction->file_size, metadata_buff + 1);

    metadata_buff[5] = source_filename_length;
    for (size_t i = 0; i < source_filename_length; i++) {
        metadata_buff[6 + i] = (transaction->source_filename.value)[i];
    }

    metadata_buff[6 + source_filename_length] = (dest_filename_length);
    for (size_t i = 0; i < dest_filename_length; i++) {
        metadata_buff[7 + i + source_filename_length] = (transaction->source_filename.value)[i];
    }

    cfdp_send(transaction, buff, metadata_size + 16);
    return 0;
}

// Note: For now this does not support segment metadata
int cfdp_send_filedata(cfdp_transaction_t *transaction, uint32_t offset, uint32_t size) {
    if (transaction == NULL)
        return -1;

    uint8_t buff[20 + size];
    cfdp_prepare_pdu_header(buff, transaction, 4 + size, CFDP_FILE_DATA);

    uint8_t *filedata_buff = buff + 16;

    uint32_to_big_endian(offset, filedata_buff);

    memcpy(filedata_buff + 4, transaction->file_data + offset, size);

    cfdp_send(transaction, buff, 20 + size);

    transaction->file_offset += size;
    return size;
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

int cfdp_send_eof(cfdp_transaction_t *transaction, uint8_t condition_code) {
    size_t fault_location_size = 0;
    if (condition_code != CFDP_COND_NOERROR) {
        fault_location_size = 6;
    }
    uint8_t buff[25 + fault_location_size];

    cfdp_prepare_pdu_header(buff, transaction, 9, CFDP_FILE_DIRECTIVE);

    uint8_t *eof_buff = buff + 16;

    eof_buff[0] = (condition_code & 0xF) << 4;
    uint32_to_big_endian(cfdp_calculate_modular_checksum(transaction), eof_buff + 1);
    uint32_to_big_endian(transaction->file_offset, eof_buff + 5);

    // We're making all entity IDs 4 Bytes, but we still have to encode TLV Format
    if (condition_code != CFDP_COND_NOERROR) {
        eof_buff[9] = 0x06;
        eof_buff[10] = 0x04;
        uint32_to_big_endian(transaction->transaction_id.entity_id, eof_buff + 11);
    }
    cfdp_send(transaction, buff, 25 + fault_location_size);
    return 0;
}

int cfdp_resend(cfdp_transaction_t *transaction) {
    while (transaction->nak_buf.size > 0) {
        cfdp_pdu_segment_request_t seg = cfdp_nak_buf_pop(&transaction->nak_buf);

        uint32_t nak_size = seg.end_offset - seg.start_offset;
        uint32_t resend_count = nak_size / SEGMENT_SIZE;

        for (uint32_t i = 0; i < resend_count; ++i) {
            cfdp_send_filedata(transaction, seg.start_offset + i * SEGMENT_SIZE, SEGMENT_SIZE);
        }

        if (nak_size % SEGMENT_SIZE > 0) {
            cfdp_send_filedata(transaction, seg.start_offset + resend_count * SEGMENT_SIZE, nak_size % SEGMENT_SIZE);
        }
    }
    return 0;
}

cfdp_result_t cfdp_transact(cfdp_transaction_t *txn, uint32_t elapsed_ms) {
    if (txn == NULL) {
        return CFDP_RESULT_INVALID_ARG;
    }

    // Already in a terminal success state (send or recv side) — nothing left to do
    if (txn->state == CFDP_SEND_STATE_DONE || txn->state == CFDP_RECV_STATE_DONE) {
        return CFDP_RESULT_COMPLETE;
    }

    // Already in a terminal error state — caller should discard the transaction
    if (txn->state == CFDP_SEND_STATE_ERR || txn->state == CFDP_RECV_STATE_ERR) {
        return CFDP_RESULT_ERROR;
    }

    // Advance the inactivity timer; if the transaction has been alive too long without
    // completing, force it into an error state (Blue Book "inactivity detected" condition)
    txn->inactivity_timer += elapsed_ms;
    if (txn->inactivity_timer >= TRANSACTION_LIFETIME_MS) {
        txn->state = (txn->direction == CFDP_SEND) ? CFDP_SEND_STATE_ERR : CFDP_RECV_STATE_ERR;
        return CFDP_RESULT_ERROR;
    }

    // Dispatch one tick of the state machine based on direction.
    // Send side: METADATA_SEND -> FILE_SEND -> WAIT_FIN/DONE
    // Recv side: FILE_RECV <-> SEND_NAK (NAK retry loop) -> DONE
    if (txn->direction == CFDP_SEND) {
        cfdp_handle_send_state(txn, elapsed_ms);
    } else {
        cfdp_handle_recv_state(txn, elapsed_ms);
    }

    // Check if this tick transitioned into a terminal success state
    if (txn->state == CFDP_SEND_STATE_DONE || txn->state == CFDP_RECV_STATE_DONE) {
        return CFDP_RESULT_COMPLETE;
    }

    // Check if this tick transitioned into a terminal error state
    if (txn->state == CFDP_SEND_STATE_ERR || txn->state == CFDP_RECV_STATE_ERR) {
        return CFDP_RESULT_ERROR;
    }

    // Transaction is still mid-flight — call again on the next tick
    return CFDP_RESULT_IN_PROGRESS;
}
