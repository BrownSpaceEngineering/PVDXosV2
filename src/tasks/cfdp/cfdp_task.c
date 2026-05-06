/**
 * adcs_task.c
 *
 * RTOS task for CFDP functionality
 *
 * Created: April 26, 2026
 * Modified: April 26, 2026
 * Authors: Noah Shepard
 */

#include "tasks/cfdp/cfdp_task.h"

#include "checks/device_checks.h"
#include "globals.h"
#include "logging.h"
#include "rtc_driver.h"
#include "task_list.h"
#include "tasks/cfdp/cfdp_pdu.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

void cfdp_put_request(cfdp_txn_type_t type, cfdp_direction_t dir) {
    cfdp_transaction_t *txn = cfdp_alloc_transaction(&cfdp_txn_store);

    if (txn == NULL) {
        debug("cfdp put: no valid transaction slots\n");
        return;
    }

    cfdp_txn_store.slot_free = false;
    for (size_t i = 0; i < MAX_TRANSACTIONS; i++) {
        if (!cfdp_txn_store.active[i]) {
            cfdp_txn_store.slot_free = true;
            break;
        }
    }

    size_t file_size = (type == IMAGE) ? IMAGE_FILE_SZ : TELEMETRY_FILE_SZ;
    cfdp_state_t state = (dir == CFDP_SEND) ? CFDP_SEND_STATE_METADATA_SEND : CFDP_RECV_STATE_WAIT_EOF;
    uint32_t seq_num = next_seq_num();
    uint8_t *file_data = (type == IMAGE) ? (uint8_t *)IMAGE_BUF : (uint8_t *)TELEMETRY_BUF;

    memset(txn, 0, sizeof(*txn));
    txn->transaction_id.entity_id = ENTITY_ID_SPACECRAFT;
    txn->transaction_id.seq_num = seq_num;
    txn->dest_entity_id = ENTITY_ID_SPACECRAFT;
    txn->file_size = file_size;
    txn->file_offset = 0;
    txn->state = state;
    txn->direction = dir;
    txn->reliable_mode = true;
    txn->file_data = file_data;
    txn->source_filename.length = 0;
    txn->source_filename.value = NULL;
    txn->dest_filename.length = 0;
    txn->dest_filename.value = NULL;
    txn->checksum_type = 0;

    return;
}

void cfdp_cancel_request(uint32_t txn_id) {
    size_t store_index = MAX_TRANSACTIONS;
    for (size_t i = 0; i < MAX_TRANSACTIONS; ++i) {
        if (cfdp_txn_store.active[i] && cfdp_txn_store.transactions[i].transaction_id.seq_num == txn_id) {
            store_index = i;
            break;
        }
    }

    if (store_index == MAX_TRANSACTIONS) {
        debug("cfdp cancel: txn_id: %lu not active\n", txn_id);
        return;
    }

    cfdp_direction_t dir = cfdp_txn_store.transactions[store_index].direction;

    cfdp_transaction_t *txn = &cfdp_txn_store.transactions[store_index];

    // Fault signaling policy for local cancel requests:
    // - Send side: transmit EOF with condition = CANCEL_REQ.
    // - Receive side: transmit Finished with condition = CANCEL_REQ.
    if (dir == CFDP_SEND) {
        cfdp_send_eof(txn, CFDP_COND_CANCEL_REQ);
    } else {
        cfdp_send_fin(txn, CFDP_COND_CANCEL_REQ);
    }

    cfdp_txn_store.transactions[store_index].state = (dir == CFDP_SEND) ? CFDP_SEND_STATE_ERR : CFDP_RECV_STATE_ERR;

    cfdp_txn_store.active[store_index] = false;
    cfdp_txn_store.slot_free = true;
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn exec_command_cfdp_request
 *
 * \brief Executes function corresponding to the command
 *
 * \param p_cmd a pointer to a command containing information for processing
 */
void exec_command_cfdp_request(command_t *const p_cmd) {
    if (p_cmd->target != p_cfdp_task) {
        fatal("cfdp request: command target is not cfdp! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
    }

    switch (p_cmd->operation) {
        case OPERATION_CFDP_PUT:
            cfdp_put_request(p_cmd->data.cfdp_request->txn_type, CFDP_SEND);
            break;
        case OPERATION_CFDP_CANCEL:
            cfdp_cancel_request(p_cmd->data.cfdp_request->txn_id);
            break;
        default:
            debug("cfdp request: invalid operation for cfdp! operation %d\n", p_cmd->operation);
    }
}

/* ---------- PDU directive handlers (called from cfdp_process_pdu) ---------- */

static void handle_filedata_pdu(cfdp_transaction_t *txn, const uint8_t *pdu_data, size_t pdu_data_sz, bool largefile,
                                bool segment_metadata_field) {
    if (txn == NULL || txn->direction != CFDP_RECV) {
        debug("cfdp: file data pdu for unknown/non-recv txn\n");
        return;
    }
    cfdp_pdu_filedata_t fd;
    if (cfdp_pdu_filedata_parse(pdu_data, pdu_data_sz, largefile, segment_metadata_field, &fd) < 0) {
        debug("cfdp: failed to parse file data pdu\n");
        return;
    }
    if (txn->file_data != NULL && fd.offset + fd.data.len <= txn->file_size) {
        memcpy(txn->file_data + fd.offset, fd.data.data, fd.data.len);
    }

    if (fd.offset == txn->file_offset) {
        txn->file_offset += fd.data.len;
    } else if (fd.offset < txn->file_offset) {
        size_t gap_index;
        while ((gap_index = cfdp_nak_buf_get_index(&txn->nak_buf, fd.offset, fd.data.len)) != CFDP_MAX_SEGMENT_REQUESTS) {
            cfdp_pdu_segment_request_t seg = txn->nak_buf.segments[gap_index];

            if (fd.offset > seg.start_offset && fd.offset + fd.data.len < seg.end_offset) {
                if (txn->nak_buf.size < CFDP_MAX_SEGMENT_REQUESTS) {
                    txn->nak_buf.segments[gap_index] =
                        (cfdp_pdu_segment_request_t){.start_offset = seg.start_offset, .end_offset = fd.offset};
                    cfdp_nak_buf_push(&txn->nak_buf,
                                      (cfdp_pdu_segment_request_t){.start_offset = fd.offset + fd.data.len, .end_offset = seg.end_offset});
                } else {
                    debug("cfdp: no room to split gap in NAK buffer, fd pdu disregarded");
                }
                break;
            } else if (fd.offset > seg.start_offset) {
                txn->nak_buf.segments[gap_index] = (cfdp_pdu_segment_request_t){.start_offset = seg.start_offset, .end_offset = fd.offset};

            } else if (fd.offset + fd.data.len < seg.end_offset) {
                txn->nak_buf.segments[gap_index] =
                    (cfdp_pdu_segment_request_t){.start_offset = fd.offset + fd.data.len, .end_offset = seg.end_offset};
                break;
            } else {
                txn->nak_buf.segments[gap_index] = txn->nak_buf.segments[txn->nak_buf.head];
                txn->nak_buf.size -= 1;
                txn->nak_buf.head = (txn->nak_buf.head == 0) ? CFDP_MAX_SEGMENT_REQUESTS - 1 : txn->nak_buf.head - 1;
            }
        }
    } else {
        if (txn->nak_buf.size < CFDP_MAX_SEGMENT_REQUESTS) {
            cfdp_nak_buf_push(&txn->nak_buf, (cfdp_pdu_segment_request_t){.start_offset = txn->file_offset, .end_offset = fd.offset});
            txn->file_offset = fd.offset + fd.data.len;
        } else {
            debug("cfdp: no room to properly update NAK buffer, therefore the fd pdu will be disregarded");
        }
    }
    txn->inactivity_timer = 0;
}

static void handle_eof_pdu(cfdp_transaction_t *txn, const uint8_t *pdu_data, uint16_t pdu_data_length) {
    if (txn == NULL || txn->direction != CFDP_RECV) {
        debug("cfdp: eof pdu for unknown/non-recv txn\n");
        return;
    }
    cfdp_pdu_eof_t eof;
    if (cfdp_pdu_eof_parse(pdu_data + 1, pdu_data_length - 1, false, &eof) < 0) {
        debug("cfdp: failed to parse eof pdu\n");
        return;
    }
    txn->file_size = eof.filesize;
    txn->expected_checksum = eof.checksum;
    txn->inactivity_timer = 0;

    if (eof.condition_code != CFDP_COND_NOERROR) {
        if (txn->radio_handle != NULL) {
            (void)cfdp_send_ack(txn, CFDP_DIR_EOF, 0, eof.condition_code, 0x02);
        }
        txn->state = CFDP_RECV_STATE_ERR;
        return;
    }

    if (txn->reliable_mode && txn->nak_buf.size > 0) {
        txn->state = CFDP_RECV_STATE_SEND_NAK;
        txn->checksum = eof.checksum;
    } else {
        uint32_t computed = (txn->file_data != NULL) ? cfdp_calculate_modular_checksum(txn) : 0;
        if (txn->checksum_type == 0 && computed != eof.checksum) {
            debug("cfdp: checksum mismatch; computed = 0x%08lx expected = 0x%08lx\n", computed, eof.checksum);
            if (txn->radio_handle != NULL) {
                cfdp_send_fin(txn, CFDP_COND_FILE_CHECKSUM_FAIL);
            }
            txn->state = CFDP_RECV_STATE_ERR;
            return;
        }
        txn->state = txn->reliable_mode ? CFDP_RECV_STATE_SEND_FIN : CFDP_RECV_STATE_DONE;
    }
}

static void handle_finished_pdu(cfdp_transaction_t *txn) {
    if (txn == NULL || txn->direction != CFDP_SEND) {
        debug("cfdp: finished pdu for unknown/non-send txn\n");
        return;
    }
    cfdp_send_ack(txn, CFDP_DIR_FINISHED, 0, CFDP_COND_NOERROR, 0x02);
    txn->state = CFDP_SEND_STATE_DONE;
    txn->inactivity_timer = 0;
}

static void handle_ack_pdu(cfdp_transaction_t *txn, const uint8_t *pdu_data, size_t pdu_data_sz) {
    if (txn == NULL) {
        debug("cfdp: ack pdu for unknown txn\n");
        return;
    }
    if (pdu_data_sz < 3) {
        debug("cfdp: ack pdu too short\n");
        return;
    }
    cfdp_pdu_ack_t ack;
    ack.directive_code = (pdu_data[1] >> 4) & 0x0F;
    ack.directive_subtype_code = pdu_data[1] & 0x0F;
    ack.condition_code = (pdu_data[2] >> 4) & 0x0F;
    ack.transaction_status = pdu_data[2] & 0x03;

    if (ack.directive_code == CFDP_DIR_EOF && txn->direction == CFDP_SEND) {
        txn->ack_timer = 0;
        txn->nak_timer = 0;
        txn->eof_retransmit_counter = 0;
        txn->state = txn->reliable_mode ? CFDP_SEND_STATE_WAIT_FIN : CFDP_SEND_STATE_DONE;
    } else if (ack.directive_code == CFDP_DIR_FINISHED && txn->direction == CFDP_RECV) {
        txn->ack_timer = 0;
        txn->state = CFDP_RECV_STATE_DONE;
    }
    txn->inactivity_timer = 0;
}

static void handle_metadata_pdu(cfdp_transaction_t *txn) {
    // Transaction allocation for new Metadata PDUs happens before the switch in cfdp_process_pdu.
    // If txn is non-NULL here the PDU is a duplicate for an already-active transaction.
    if (txn != NULL) {
        debug("cfdp: duplicate metadata pdu for existing txn \xe2\x80\x94 ignored\n");
    }
}

static void handle_nak_pdu(cfdp_transaction_t *txn, const uint8_t *pdu_data, size_t pdu_data_sz) {
    if (txn == NULL || txn->direction != CFDP_SEND) {
        debug("cfdp: nak pdu for unknown/non-send txn\n");
        return;
    }
    if (pdu_data_sz < 9) {
        debug("cfdp: nak pdu too short\n");
        return;
    }
    const uint8_t *nak_body = pdu_data + 1;
    size_t nak_body_sz = pdu_data_sz - 1;
    uint32_t seg_count = (nak_body_sz - 8) / 8;
    for (uint32_t i = 0; i < seg_count && i < CFDP_MAX_SEGMENT_REQUESTS; i++) {
        const uint8_t *s = nak_body + 8 + (i * 8);
        cfdp_pdu_segment_request_t seg;
        seg.start_offset = ((uint32_t)s[0] << 24) | ((uint32_t)s[1] << 16) | ((uint32_t)s[2] << 8) | s[3];
        seg.end_offset = ((uint32_t)s[4] << 24) | ((uint32_t)s[5] << 16) | ((uint32_t)s[6] << 8) | s[7];
        cfdp_nak_buf_push(&txn->nak_buf, seg);
    }
    txn->state = CFDP_SEND_STATE_FILE_SEND;
    txn->inactivity_timer = 0;
}

/**
 * \fn cfdp_process_pdu
 *
 * \brief Processes incoming raw PDU and updates cfdp_txn_store
 *
 * \param raw a pointer to the incoming data
 * \param sz the size in octets of the data
 */
void cfdp_process_pdu(uint8_t *raw, size_t sz) {
    cfdp_pdu_header_t header;
    size_t header_size;
    const uint8_t *pdu_data;
    size_t pdu_data_sz;
    int bytes_read = cfdp_pdu_header_parse(raw, sz, &header);

    if (bytes_read < 0) {
        debug("cfdp: incoming pdu of invalid size\n");
        return;
    }

    header_size = (size_t)bytes_read;
    pdu_data = raw + header_size;
    pdu_data_sz = sz - header_size;

    // get PDU type
    uint8_t dir_code = 0;

    if (header.pdu_type == 0) {
        dir_code = raw[header_size];
    }

    cfdp_transaction_t *txn = NULL;
    for (size_t i = 0; i < MAX_TRANSACTIONS; i++) {
        if (cfdp_txn_store.active[i] && cfdp_txn_store.transactions[i].transaction_id.entity_id == header.source_entity_id &&
            cfdp_txn_store.transactions[i].transaction_id.seq_num == header.transaction_seq) {
            txn = &cfdp_txn_store.transactions[i];
            break;
        }
    }
    if (txn == NULL) {
        if (dir_code != CFDP_DIR_METADATA) {
            debug("cfdp: unknown incoming pdu of type %x disregarded\n", dir_code);
            return;
        }

        if (!cfdp_txn_store.slot_free) {
            debug("cfdp: txn store full, unable to accept sequence: %x, from entity: %x\n", header.transaction_seq,
                  header.source_entity_id); // we should probably transmit an error finished
            return;
        }

        // create new receive-side transaction from incoming Metadata PDU
        if (pdu_data_sz < 2) {
            debug("cfdp: metadata pdu too short to parse\n");
            return;
        }
        // Skip the directive code byte before parsing the metadata body.
        cfdp_pdu_metadata_t meta;
        if (cfdp_pdu_metadata_parse(pdu_data + 1, pdu_data_sz - 1, &meta) < 0) {
            debug("cfdp: failed to parse metadata pdu\n");
            return;
        }

        cfdp_transaction_t *new_txn = cfdp_alloc_transaction(&cfdp_txn_store);
        if (new_txn == NULL) {
            debug("cfdp: alloc failed for new recv txn\n");
            return;
        }
        new_txn->transaction_id.entity_id = header.source_entity_id;
        new_txn->transaction_id.seq_num = header.transaction_seq;
        new_txn->dest_entity_id = header.dest_entity_id;
        new_txn->file_size = meta.file_length;
        new_txn->file_offset = 0;
        new_txn->state = CFDP_RECV_STATE_WAIT_EOF;
        new_txn->direction = CFDP_RECV;
        new_txn->reliable_mode = (meta.closure_req != 0);
        new_txn->inactivity_timer = 0;
        new_txn->ack_timer = 0;
        new_txn->nak_timer = 0;
        new_txn->eof_retransmit_counter = 0;
        new_txn->nak_retransmit_counter = 0;
        new_txn->nak_buf.head = 0;
        new_txn->nak_buf.tail = 0;
        new_txn->nak_buf.size = 0;
        memset(new_txn->received_bitmap, 0, sizeof(new_txn->received_bitmap));
        new_txn->source_filename = (cfdp_lv_t){.length = 0, .value = NULL};
        new_txn->dest_filename = (cfdp_lv_t){.length = 0, .value = NULL};
        new_txn->radio_handle = NULL; // not needed for recv side
        new_txn->checksum_type = meta.checksum_type;

        uint8_t *data = NULL;

        if (meta.file_length <= CFDP_SMALL_BUFF_SZ) {
            data = cfdp_alloc_small_buff();
        } else if (meta.file_length <= CFDP_LARGE_BUFF_SZ) {
            data = cfdp_alloc_large_buff();
        } else {
            debug("cfdp: incoming file larger than supported size\n");
        }

        if (data == NULL) {
            debug("cfdp: no free buffer to allocate\n");
        }

        new_txn->file_data = data;

        txn = new_txn;
        // Fall through into the switch — dir_code == CFDP_DIR_METADATA will hit the duplicate-guard and return cleanly.
    }

    switch (dir_code) {
        case 0:
            handle_filedata_pdu(txn, pdu_data, pdu_data_sz, header.largefile, header.segment_metadata_field);
            break;
        case CFDP_DIR_EOF:
            handle_eof_pdu(txn, pdu_data, header.pdu_data_length);
            break;
        case CFDP_DIR_FINISHED:
            handle_finished_pdu(txn);
            break;
        case CFDP_DIR_ACK:
            handle_ack_pdu(txn, pdu_data, pdu_data_sz);
            break;
        case CFDP_DIR_METADATA:
            handle_metadata_pdu(txn);
            break;
        case CFDP_DIR_NAK:
            handle_nak_pdu(txn, pdu_data, pdu_data_sz);
            break;
        default:
            debug("cfdp: unrecognized directive code: %x", dir_code);
            return;
    }
}

uint32_t next_seq_num(void) {
    static uint32_t counter = 0;
    return ++counter;
}

cfdp_transaction_t *cfdp_send_init(cfdp_transaction_store_t *txn_store, uint8_t *fl, uint32_t sz, cfdp_lv_t source_filename,
                                   cfdp_lv_t dest_filename, uint32_t source_entity_id, uint32_t dest_entity_id, uint8_t channel_num,
                                   uint8_t priority, bool reliable_mode, at86rf215_t *radio_handle) {
    if (txn_store == NULL || fl == NULL || sz == 0 || radio_handle == NULL) {
        return NULL;
    }

    cfdp_transaction_t *txn = cfdp_alloc_transaction(txn_store);
    if (txn == NULL) {
        return NULL;
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
    return cfdp_send_init(txn_store, fl, sz, empty, empty, ENTITY_ID_SPACECRAFT, ENTITY_ID_GROUND, 0, 0, true, radio_handle) != NULL ? 0
                                                                                                                                     : -1;
}

/* ---------- CFDP State Machines ---------- */

cfdp_result_t cfdp_handle_send_state(cfdp_transaction_t *transaction, uint32_t elapsed_ms) {
    switch (transaction->state) {
        case CFDP_SEND_STATE_METADATA_SEND:
            cfdp_send_metadata(transaction);
            return CFDP_RESULT_IN_PROGRESS;
        case CFDP_SEND_STATE_FILE_SEND:
            if (transaction->nak_buf.size > 0) {
                cfdp_resend(transaction);
            } else if (transaction->file_offset < transaction->file_size) {
                uint32_t remaining = transaction->file_size - transaction->file_offset;
                uint32_t chunk_size = (remaining < SEGMENT_SIZE) ? remaining : SEGMENT_SIZE;
                cfdp_send_filedata(transaction, transaction->file_offset, chunk_size);
            } else {
                cfdp_send_eof(transaction, CFDP_COND_NOERROR);
                if (transaction->reliable_mode) {
                    transaction->state = CFDP_SEND_STATE_WAIT_ACK;
                    transaction->ack_timer = 0;
                    transaction->eof_retransmit_counter = 0;
                } else {
                    transaction->state = CFDP_SEND_STATE_DONE;
                }
            }
            return CFDP_RESULT_IN_PROGRESS;
        case CFDP_SEND_STATE_WAIT_ACK:
            transaction->ack_timer += elapsed_ms;
            if (transaction->ack_timer > ACK_TIMEOUT_MS) {
                transaction->ack_timer = 0;
                if (transaction->eof_retransmit_counter >= ACK_RETRANSMIT_LIMIT) {
                    transaction->state = CFDP_SEND_STATE_ERR;
                } else {
                    transaction->eof_retransmit_counter++;
                    cfdp_send_eof(transaction, CFDP_COND_NOERROR);
                }
            }
            return CFDP_RESULT_BLOCKED;
        case CFDP_SEND_STATE_WAIT_FIN:
            transaction->nak_timer += elapsed_ms;
            if (transaction->nak_timer > NAK_TIMEOUT_MS) {
                transaction->nak_timer = 0;
                if (transaction->eof_retransmit_counter >= ACK_RETRANSMIT_LIMIT) {
                    transaction->state = CFDP_SEND_STATE_ERR;
                } else {
                    transaction->eof_retransmit_counter++;
                    cfdp_send_eof(transaction, CFDP_COND_NOERROR);
                }
            }
            return CFDP_RESULT_BLOCKED;
        case CFDP_SEND_STATE_DONE:
            return CFDP_RESULT_COMPLETE;
        case CFDP_SEND_STATE_ERR:
            return CFDP_RESULT_ERROR;
        default:
            return CFDP_RESULT_ERROR;
    }
}

cfdp_result_t cfdp_handle_recv_state(cfdp_transaction_t *transaction, uint32_t elapsed_ms) {
    switch (transaction->state) {
        case CFDP_RECV_STATE_WAIT_EOF:
            return CFDP_RESULT_BLOCKED;
        case CFDP_RECV_STATE_SEND_NAK:
            cfdp_send_nak(transaction);
            if (transaction->nak_buf.size == 0) {
                transaction->state = CFDP_RECV_STATE_WAIT_RETRANSMIT;
            }
            transaction->ack_timer = 0;
            return CFDP_RESULT_IN_PROGRESS;
        case CFDP_RECV_STATE_WAIT_RETRANSMIT:
            if (transaction->nak_buf.size == 0) {
                transaction->state = CFDP_RECV_STATE_SEND_FIN;
            }
            transaction->nak_timer += elapsed_ms;
            if (transaction->nak_timer > NAK_TIMEOUT_MS) {
                transaction->nak_timer = 0;
                if (transaction->nak_retransmit_counter >= NAK_RETRANSMIT_LIMIT) {
                    transaction->state = CFDP_RECV_STATE_ERR;
                } else {
                    transaction->nak_retransmit_counter++;
                    transaction->state = CFDP_RECV_STATE_SEND_NAK;
                }
            }
            return CFDP_RESULT_BLOCKED;
        case CFDP_RECV_STATE_SEND_FIN:
            cfdp_send_fin(transaction, CFDP_COND_NOERROR);
            transaction->nak_retransmit_counter++;
            if (transaction->nak_retransmit_counter > NAK_RETRANSMIT_LIMIT) {
                transaction->state = CFDP_RECV_STATE_ERR;
            } else {
                transaction->state = CFDP_RECV_STATE_WAIT_FIN_ACK;
                transaction->ack_timer = 0;
            }
            return CFDP_RESULT_BLOCKED;
        case CFDP_RECV_STATE_WAIT_FIN_ACK:
            transaction->ack_timer += elapsed_ms;
            if (transaction->ack_timer > ACK_TIMEOUT_MS) {
                transaction->ack_timer = 0;
                transaction->state = CFDP_RECV_STATE_SEND_FIN;
            }
            return CFDP_RESULT_BLOCKED;
        case CFDP_RECV_STATE_DONE:
            return CFDP_RESULT_COMPLETE;
        case CFDP_RECV_STATE_ERR:
            return CFDP_RESULT_ERROR;
        default:
            return CFDP_RESULT_ERROR;
    }
}

void cfdp_send(cfdp_transaction_t *transaction, const uint8_t *buff, size_t sz) {
    at86rf215_tx_frame(transaction->radio_handle, (at86rf215_radio_t)transaction->channel_num, buff, sz, TX_TIMEOUT_MS);
}

int send(void *buff, size_t sz) {
    (void)buff;
    (void)sz;
    return 0;
}

int recv(void *buff, size_t sz) {
    (void)buff;
    (void)sz;
    return 0;
}

/* ---------- CFDP Utility and PDU Build/Send ---------- */

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
    for (size_t i = 0; i < CFDP_MAX_SEGMENT_REQUESTS; ++i) {
        if (buf->segments[buf->tail + i % CFDP_MAX_SEGMENT_REQUESTS].start_offset <= offset + len &&
            buf->segments[buf->tail + i % CFDP_MAX_SEGMENT_REQUESTS].end_offset >= offset) {
            return buf->tail + i % CFDP_MAX_SEGMENT_REQUESTS;
        }
    }
    return CFDP_MAX_SEGMENT_REQUESTS;
}

cfdp_transaction_t *cfdp_alloc_transaction(cfdp_transaction_store_t *txn_store) {
    if (!txn_store->slot_free) {
        return NULL;
    }
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        if (!txn_store->active[i]) {
            txn_store->active[i] = true;
            memset(&txn_store->transactions[i], 0, sizeof(cfdp_transaction_t));
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
    debug("cfdp: mismatch between slot_free and txn store capacity");
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
            return &cfdp_small_buffs.buff[i * CFDP_SMALL_BUFF_SZ];
        }
    }
    return NULL;
}

uint8_t *cfdp_alloc_large_buff() {
    if (cfdp_large_buff.in_use) {
        return NULL;
    } else {
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

cfdp_result_t cfdp_transact(cfdp_transaction_t *txn, uint32_t elapsed_ms) {
    if (txn == NULL) {
        return CFDP_RESULT_INVALID_ARG;
    }

    txn->inactivity_timer += elapsed_ms;
    if (txn->inactivity_timer >= TRANSACTION_LIFETIME_MS) {
        txn->state = (txn->direction == CFDP_SEND) ? CFDP_SEND_STATE_ERR : CFDP_RECV_STATE_ERR;
        return CFDP_RESULT_ERROR;
    }

    cfdp_result_t result = CFDP_RESULT_ERROR;

    if (txn->direction == CFDP_SEND) {
        result = cfdp_handle_send_state(txn, elapsed_ms);
    } else {
        result = cfdp_handle_recv_state(txn, elapsed_ms);
    }

    return result;
}
