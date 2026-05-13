/**
 * cfdp_task.c
 *
 * RTOS task for CFDP functionality
 *
 * Created: April 26, 2026
 * Modified: May 10, 2026
 * Authors: Noah Shepard, Avinash Patel
 */

#include "tasks/cfdp/cfdp_task.h"

#include "checks/device_checks.h"
#include "globals.h"
#include "logging.h"
#include "rtc_driver.h"
#include "task_list.h"
#include "tasks/cfdp/cfdp_pdu.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

void cfdp_put_request(cfdp_put_data_t put_data) {
    cfdp_transaction_t *txn = cfdp_alloc_transaction(&cfdp_txn_store);

    if (txn == NULL) {
        warning("cfdp put: no valid transaction slots\n");
        return;
    }

    cfdp_txn_store.slot_free = false;
    for (size_t i = 0; i < MAX_TRANSACTIONS; i++) {
        if (!cfdp_txn_store.active[i]) {
            cfdp_txn_store.slot_free = true;
            break;
        }
    }

    size_t file_size = (put_data.txn_type == IMAGE) ? IMAGE_FILE_SZ : TELEMETRY_FILE_SZ;

    uint32_t seq_num = next_seq_num();

    memset(txn, 0, sizeof(*txn));
    txn->transaction_id.entity_id = ENTITY_ID_SPACECRAFT;
    txn->transaction_id.seq_num = seq_num;
    txn->type = put_data.txn_type;
    txn->dest_entity_id = ENTITY_ID_SPACECRAFT;
    txn->file_size = file_size;
    txn->file_offset = 0;
    txn->state = CFDP_SEND_STATE_METADATA_SEND;
    txn->direction = CFDP_SEND;
    txn->reliable_mode = true;
    txn->source_filename.length = 0;
    txn->source_filename.value = NULL;
    txn->dest_filename.length = 0;
    txn->dest_filename.value = NULL;
    txn->file_data = put_data.memory;
    txn->checksum_type = 0;
    txn->ack_retransmit_counter = 0;
    txn->nak_retransmit_counter = 0;
    txn->inactivity_timer_handle = xTimerCreateStatic("Inactivity Timer", pdMS_TO_TICKS(TRANSACTION_LIFETIME_MS), pdFALSE, (void *)txn,
                                                      &inactivity_timer_callback, &txn->inactivity_timer_mem);

    start_timer(txn->inactivity_timer_handle);

    txn->ack_timer_handle =
        xTimerCreateStatic("EOF ACK Timer", pdMS_TO_TICKS(ACK_TIMEOUT_MS), pdTRUE, (void *)txn, &ack_timer_callback, &txn->ack_timer_mem);

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
        warning("cfdp cancel: txn_id: %lu not active\n", txn_id);
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

    stop_timer(txn->inactivity_timer_handle);
    stop_timer(txn->ack_timer_handle);

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
            cfdp_put_request(p_cmd->data.cfdp_request->put_data);
            break;
        case OPERATION_CFDP_CANCEL:
            cfdp_cancel_request(p_cmd->data.cfdp_request->txn_id);
            break;
        default:
            warning("cfdp request: invalid operation for cfdp! operation %d\n", p_cmd->operation);
    }
}

/* ---------- PDU directive handlers (called from cfdp_process_pdu) ---------- */

static void handle_filedata_pdu(cfdp_transaction_t *txn, const uint8_t *pdu_data, size_t pdu_data_sz, bool largefile,
                                bool segment_metadata_field) {
    if (txn == NULL || txn->direction != CFDP_RECV) {
        warning("cfdp: file data pdu for unknown/non-recv txn\n");
        return;
    }
    cfdp_pdu_filedata_t fd;
    if (cfdp_pdu_filedata_parse(pdu_data, pdu_data_sz, largefile, segment_metadata_field, &fd) < 0) {
        warning("cfdp: failed to parse file data pdu\n");
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
                    warning("cfdp: no room to split gap in NAK buffer, fd pdu disregarded");
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
            warning("cfdp: no room to properly update NAK buffer, therefore the fd pdu will be disregarded");
        }
    }

    reset_timer(txn->inactivity_timer_handle);
}

static void handle_eof_pdu(cfdp_transaction_t *txn, const uint8_t *pdu_data, uint16_t pdu_data_length) {
    if (txn == NULL || txn->direction != CFDP_RECV) {
        warning("cfdp: eof pdu for unknown/non-recv txn\n");
        return;
    }
    cfdp_pdu_eof_t eof;
    if (cfdp_pdu_eof_parse(pdu_data, pdu_data_length, false, &eof) < 0) {
        warning("cfdp: failed to parse eof pdu\n");
        return;
    }
    txn->file_size = eof.filesize;
    txn->expected_checksum = eof.checksum;
    reset_timer(txn->inactivity_timer_handle);

    if (eof.condition_code != CFDP_COND_NOERROR) {
        cfdp_send_ack(txn, CFDP_DIR_EOF, 0, eof.condition_code, 0x01);
        txn->state = CFDP_RECV_STATE_ERR;
        return;
    }

    if (txn->reliable_mode && txn->nak_buf.size > 0) {
        txn->state = CFDP_RECV_STATE_SEND_NAK;
        txn->checksum = eof.checksum;
        cfdp_send_ack(txn, CFDP_DIR_EOF, 0, CFDP_COND_NOERROR, 0x01);
        start_timer(txn->nak_timer_handle);

    } else {
        uint32_t computed = (txn->file_data != NULL) ? cfdp_calculate_modular_checksum(txn) : 0;
        if (txn->checksum_type == 0 && computed != eof.checksum) {
            warning("cfdp: checksum mismatch; computed = 0x%08lx expected = 0x%08lx\n", computed, eof.checksum);
            cfdp_send_fin(txn, CFDP_COND_FILE_CHECKSUM_FAIL);
            txn->state = CFDP_RECV_STATE_ERR;
            return;
        }
        txn->state = txn->reliable_mode ? CFDP_RECV_STATE_SEND_FIN : CFDP_RECV_STATE_DONE;
    }
}

static void handle_finished_pdu(cfdp_transaction_t *txn, const uint8_t *pdu_data, size_t pdu_data_sz) {
    if (txn == NULL || txn->direction != CFDP_SEND) {
        warning("cfdp: finished pdu for unknown/non-send txn\n");
        return;
    }

    cfdp_pdu_finished_t fin;
    cfdp_pdu_finished_parse(pdu_data, pdu_data_sz, &fin);

    if (fin.condition_code != CFDP_COND_NOERROR) {
        txn->state = CFDP_SEND_STATE_ERR;
        cfdp_send_ack(txn, CFDP_DIR_FINISHED, 0, fin.condition_code, 0x01);
    } else {
        txn->state = CFDP_SEND_STATE_DONE;
        cfdp_send_ack(txn, CFDP_DIR_FINISHED, 0, fin.condition_code, 0x01);
    }

    reset_timer(txn->inactivity_timer_handle);
}

static void handle_ack_pdu(cfdp_transaction_t *txn, const uint8_t *pdu_data, size_t pdu_data_sz) {
    if (txn == NULL) {
        warning("cfdp: ack pdu for unknown txn\n");
        return;
    }
    if (pdu_data_sz < 3) {
        warning("cfdp: ack pdu too short\n");
        return;
    }
    cfdp_pdu_ack_t ack;
    cfdp_pdu_ack_parse(pdu_data, pdu_data_sz, &ack);

    ack.directive_code = (pdu_data[1] >> 4) & 0x0F;
    ack.directive_subtype_code = pdu_data[1] & 0x0F;
    ack.condition_code = (pdu_data[2] >> 4) & 0x0F;
    ack.transaction_status = pdu_data[2] & 0x03;

    stop_timer(txn->ack_timer_handle);
    txn->ack_retransmit_counter = 0;

    if (ack.directive_code == CFDP_DIR_EOF && txn->direction == CFDP_SEND) {
        txn->state = txn->reliable_mode ? CFDP_SEND_STATE_WAIT_FIN : CFDP_SEND_STATE_DONE;
    } else if (ack.directive_code == CFDP_DIR_FINISHED && txn->direction == CFDP_RECV) {
        txn->state = CFDP_RECV_STATE_DONE;
    }

    reset_timer(txn->inactivity_timer_handle);
}

static void handle_metadata_pdu(cfdp_transaction_t *txn) {
    // Transaction allocation for new Metadata PDUs happens before the switch in cfdp_process_pdu.
    // If txn is non-NULL here the PDU is a duplicate for an already-active transaction.
    if (txn != NULL) {
        warning("cfdp: duplicate metadata pdu for existing txn \xe2\x80\x94 ignored\n");
    }
}

static void handle_nak_pdu(cfdp_transaction_t *txn, const uint8_t *pdu_data, size_t pdu_data_sz) {
    if (txn == NULL || txn->direction != CFDP_SEND) {
        warning("cfdp: nak pdu for unknown/non-send txn\n");
        return;
    }
    if (pdu_data_sz < 8) {
        warning("cfdp: nak pdu too short\n");
        return;
    }
    const uint8_t *nak_body = pdu_data;
    size_t nak_body_sz = pdu_data_sz;
    uint32_t seg_count = (nak_body_sz - 8) / 8;
    for (uint32_t i = 0; i < seg_count && i < CFDP_MAX_SEGMENT_REQUESTS; i++) {
        const uint8_t *s = nak_body + 8 + (i * 8);
        cfdp_pdu_segment_request_t seg;
        seg.start_offset = ((uint32_t)s[0] << 24) | ((uint32_t)s[1] << 16) | ((uint32_t)s[2] << 8) | s[3];
        seg.end_offset = ((uint32_t)s[4] << 24) | ((uint32_t)s[5] << 16) | ((uint32_t)s[6] << 8) | s[7];
        cfdp_nak_buf_push(&txn->nak_buf, seg);
    }
    txn->state = CFDP_SEND_STATE_FILE_SEND;
    reset_timer(txn->inactivity_timer_handle);
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
        warning("cfdp: incoming pdu of invalid size\n");
        return;
    }

    header_size = (size_t)bytes_read;
    pdu_data = raw + header_size;
    pdu_data_sz = sz - header_size;

    // get PDU type
    uint8_t dir_code = 0;

    if (header.pdu_type == 0) {
        dir_code = raw[header_size];
        pdu_data++;
        pdu_data_sz--;
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
        if (dir_code == CFDP_DIR_METADATA || dir_code == CFDP_DIR_EOF) {
            cfdp_send_metadata_nak(&header);
        } else if (dir_code != CFDP_DIR_METADATA) {
            warning("cfdp: unable to process incoming pdu of type %x from unrecognized transaction", dir_code);
            return;
        }

        // create new receive-side transaction from incoming Metadata PDU
        if (pdu_data_sz < 2) {
            warning("cfdp: metadata pdu too short to parse\n");
            return;
        }
        // Skip the directive code byte before parsing the metadata body.
        cfdp_pdu_metadata_t meta;
        if (cfdp_pdu_metadata_parse(pdu_data, pdu_data_sz, &meta) < 0) {
            warning("cfdp: failed to parse metadata pdu\n");
            return;
        }

        if (header.largefile != 0 || header.segmentation_control != 0) {
            warning("cfdp: unsupported transaction type, rejecting");
            // need to send a fin here to show that we recieved the transaction, but are not accepting it
            cfdp_send_reject_fin(&header, &meta, CFDP_COND_INVALID_TRANSMISSION);
            return;
        }

        if (meta.checksum_type != 0x00 || meta.checksum_type != 0x0F) {
            warning("cfdp: unsported checksum type, rejecting");
            cfdp_send_reject_fin(&header, &meta, CFDP_COND_BAD_CHECKSUM);
        }

        cfdp_handle_metadata_opt(&meta);

        cfdp_transaction_t *new_txn = cfdp_alloc_transaction(&cfdp_txn_store);
        if (new_txn == NULL) {
            warning("cfdp: alloc failed for new recv txn\n");
            cfdp_send_reject_fin(&header, &meta, CFDP_COND_CANCEL_REQ);
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
        new_txn->ack_retransmit_counter = 0;
        new_txn->nak_retransmit_counter = 0;
        new_txn->nak_buf.head = 0;
        new_txn->nak_buf.tail = 0;
        new_txn->nak_buf.size = 0;
        new_txn->source_filename = (cfdp_lv_t){.length = 0, .value = NULL};
        new_txn->dest_filename = (cfdp_lv_t){.length = 0, .value = NULL};
        new_txn->checksum_type = meta.checksum_type;

        new_txn->inactivity_timer_handle = xTimerCreateStatic("Inactivity Timer", pdMS_TO_TICKS(TRANSACTION_LIFETIME_MS), pdFALSE,
                                                              (void *)new_txn, &inactivity_timer_callback, &new_txn->inactivity_timer_mem);

        start_timer(new_txn->inactivity_timer_handle);

        new_txn->ack_timer_handle = xTimerCreateStatic("FIN ACK Timer", pdMS_TO_TICKS(TRANSACTION_LIFETIME_MS), pdTRUE, (void *)new_txn,
                                                       &ack_timer_callback, &new_txn->ack_timer_mem);

        new_txn->nak_timer_handle = xTimerCreateStatic("NAK Timer", pdMS_TO_TICKS(TRANSACTION_LIFETIME_MS), pdTRUE, (void *)new_txn,
                                                       &nak_timer_callback, &new_txn->nak_timer_mem);

        uint8_t *data = NULL;

        if (meta.file_length <= CFDP_SMALL_BUFF_SZ) {
            data = cfdp_alloc_small_buff();
        } else if (meta.file_length <= CFDP_LARGE_BUFF_SZ) {
            data = cfdp_alloc_large_buff();
        } else {
            warning("cfdp: incoming file larger than supported size\n");
            cfdp_send_reject_fin(&header, &meta, CFDP_COND_FILE_SIZEERROR);
            return;
        }

        if (data == NULL) {
            warning("cfdp: no free buffer to allocate\n");
            cfdp_send_reject_fin(&header, &meta, CFDP_COND_INVALID_TRANSMISSION);
            return;
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
            handle_finished_pdu(txn, pdu_data, header.pdu_data_length);
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
            warning("cfdp: unrecognized directive code: %x", dir_code);
            return;
    }
}

uint32_t next_seq_num(void) {
    static uint32_t counter = 0;
    return ++counter;
}

/* ---------- CFDP State Machines ---------- */

cfdp_result_t cfdp_handle_send_state(cfdp_transaction_t *transaction, uint32_t elapsed_ms) {
    switch (transaction->state) {
        case CFDP_SEND_STATE_METADATA_SEND:
            cfdp_send_metadata(transaction);
            transaction->state = CFDP_SEND_STATE_FILE_SEND;
            return CFDP_RESULT_IN_PROGRESS;
        case CFDP_SEND_STATE_FILE_SEND:
            if (transaction->reliable_mode && transaction->nak_buf.size > 0) {
                cfdp_resend(transaction);
            } else if (transaction->file_offset < transaction->file_size) {
                uint32_t remaining = transaction->file_size - transaction->file_offset;
                uint32_t chunk_size = (remaining < SEGMENT_SIZE) ? remaining : SEGMENT_SIZE;
                cfdp_send_filedata(transaction, transaction->file_offset, chunk_size);
                transaction->file_offset += chunk_size;
            } else {
                cfdp_send_eof(transaction, CFDP_COND_NOERROR);
                if (transaction->reliable_mode) {
                    transaction->state = CFDP_SEND_STATE_WAIT_ACK;
                    start_timer(transaction->ack_timer_handle);
                    transaction->ack_retransmit_counter = 0;
                } else {
                    transaction->state = CFDP_SEND_STATE_DONE;
                }
            }
            return CFDP_RESULT_IN_PROGRESS;
        case CFDP_SEND_STATE_WAIT_ACK:
            return CFDP_RESULT_BLOCKED;
        case CFDP_SEND_STATE_WAIT_FIN:
            if (transaction->reliable_mode && transaction->nak_buf.size > 0) {
                cfdp_resend(transaction);
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
            return CFDP_RESULT_IN_PROGRESS;
        case CFDP_RECV_STATE_WAIT_RETRANSMIT:
            if (transaction->nak_buf.size == 0) {
                stop_timer(transaction->nak_timer_handle);
                transaction->state = CFDP_RECV_STATE_SEND_FIN;
            }
            return CFDP_RESULT_BLOCKED;
        case CFDP_RECV_STATE_SEND_FIN:
            cfdp_send_fin(transaction, CFDP_COND_NOERROR);
            transaction->state = CFDP_RECV_STATE_WAIT_FIN_ACK;
            start_timer(transaction->ack_timer_handle);
            transaction->ack_retransmit_counter = 0;

            return CFDP_RESULT_BLOCKED;
        case CFDP_RECV_STATE_WAIT_FIN_ACK:
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
    // at86rf215_tx_frame(transaction->radio_handle, (at86rf215_radio_t)transaction->channel_num, buff, sz, TX_TIMEOUT_MS);
    (void)transaction;
    (void)buff;
    debug("sending %lu bytes", sz);
    return;
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
    for (size_t i = 0; i < buf->size; ++i) {
        if (buf->segments[(buf->tail + i) % CFDP_MAX_SEGMENT_REQUESTS].start_offset <= offset + len &&
            buf->segments[(buf->tail + i) % CFDP_MAX_SEGMENT_REQUESTS].end_offset >= offset) {
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

cfdp_result_t cfdp_transact(cfdp_transaction_t *txn, uint32_t elapsed_ms) {
    if (txn == NULL) {
        return CFDP_RESULT_INVALID_ARG;
    }

    cfdp_result_t result = CFDP_RESULT_ERROR;

    if (txn->direction == CFDP_SEND) {
        result = cfdp_handle_send_state(txn, elapsed_ms);
    } else {
        result = cfdp_handle_recv_state(txn, elapsed_ms);
    }

    return result;
}

// Timer Functions
// NOTE: This code will run in the internal timer task -- if cfdp cares about only sending/recving at one time we need to rework this
// to send a command to cfdp

void inactivity_timer_callback(TimerHandle_t inactivity_timer_handle) {
    cfdp_transaction_t *txn = (cfdp_transaction_t *)pvTimerGetTimerID(inactivity_timer_handle);

    if (txn->direction == CFDP_SEND) {
        txn->state = CFDP_SEND_STATE_ERR;
        cfdp_send_eof(txn, CFDP_COND_INACTIVITY);
    } else {
        txn->state = CFDP_RECV_STATE_ERR;
        cfdp_send_fin(txn, CFDP_COND_INACTIVITY);
    }
}

void ack_timer_callback(TimerHandle_t ack_timer_handle) {
    cfdp_transaction_t *txn = (cfdp_transaction_t *)pvTimerGetTimerID(ack_timer_handle);

    uint8_t cond = CFDP_COND_NOERROR;

    if (txn->ack_retransmit_counter >= ACK_RETRANSMIT_LIMIT) {
        if (xTimerStop(ack_timer_handle, 0) != pdPASS) {
            warning("timer: unable to stop ack timer even though retransmit limit reached");
            return; // timer double counts, but it gives time for the timer queue to process
        }

        txn->state = (txn->direction == CFDP_SEND) ? CFDP_SEND_STATE_ERR : CFDP_RECV_STATE_ERR;

        cond = CFDP_COND_ACK_LIMIT;
    }

    if (txn->direction == CFDP_SEND) { // EOF-ACK Timer
        cfdp_send_eof(txn, cond);
    } else { // FIN-ACK Timer
        cfdp_send_fin(txn, cond);
    }

    txn->ack_retransmit_counter++;
}

void nak_timer_callback(TimerHandle_t nak_timer_handle) {
    cfdp_transaction_t *txn = (cfdp_transaction_t *)pvTimerGetTimerID(nak_timer_handle);

    if (txn->direction != CFDP_RECV) {
        warning("timer: somehow started NAK timer for a send transaction, attempting to stop");
        xTimerStop(nak_timer_handle, 0);
        return;
    }

    if (txn->nak_retransmit_counter >= NAK_RETRANSMIT_LIMIT) {
        cfdp_send_fin(txn, CFDP_COND_NAK_LIMIT);

        if (xTimerStart(txn->ack_timer_handle, 0) != pdPASS) {
            warning("timer: unable to start fin timer, even though one was sent");
        }

        return;
    }

    cfdp_send_nak(txn);
    txn->nak_retransmit_counter++;
}
