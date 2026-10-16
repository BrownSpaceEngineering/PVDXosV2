/**
 * cfdp_task.c
 *
 * RTOS task for CFDP functionality
 *
 * Created: April 26, 2026
 * Modified: May 10, 2026
 * Authors: Noah Shepard, Avinash Patel
 */

#include "cfdp_task.h"

#include <string.h>

#include "cfdp_pdu.h"
#include "cfdp_timer.h"
#include "cfdp_utils.h"
#include "checks/device_checks.h"
#include "globals.h"
#include "logging.h"
#include "rtc_driver.h"
#include "task_list.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command
 * dispatcher task) ---------- */

void cfdp_put_request(cfdp_put_data_t *put_data) {
    cfdp_transaction_t *txn = cfdp_alloc_transaction(&cfdp_txn_store);

    if (txn == NULL) {
        warning("cfdp put: no valid transaction slots\n");
        return;
    }

    info("cfdp put: allocated txn %p\n", txn);

    cfdp_txn_store.slot_free = false;
    for (size_t i = 0; i < MAX_TRANSACTIONS; i++) {
        if (!cfdp_txn_store.active[i]) {
            cfdp_txn_store.slot_free = true;
            break;
        }
    }

    // size_t file_size = (put_data.txn_type == IMAGE) ? IMAGE_FILE_SZ :
    // TELEMETRY_FILE_SZ;

    uint32_t seq_num = next_seq_num();

    info("cfdp_put: seq #%u\n", seq_num);

    memset(txn, 0, sizeof(*txn));
    txn->transaction_id.entity_id = ENTITY_ID_SPACECRAFT;
    txn->transaction_id.seq_num = seq_num;
    txn->type = put_data->txn_type;
    txn->dest_entity_id = ENTITY_ID_SPACECRAFT;
    txn->file_size = put_data->file_size;
    txn->file_offset = 0;
    txn->state = CFDP_SEND_STATE_METADATA_SEND;
    txn->direction = CFDP_SEND;
    txn->reliable_mode = true;
    txn->source_filename.length = 0;
    txn->source_filename.value = NULL;
    txn->dest_filename.length = 0;
    txn->dest_filename.value = NULL;
    txn->file_data = put_data->memory;
    txn->checksum_type = 0;
    txn->ack_retransmit_counter = 0;
    txn->nak_retransmit_counter = 0;
    txn->condition_code = CFDP_COND_NOERROR;
    txn->eof_acked = false;
    // TODO : Link handles -> Txn Struct
    txn->inactivity_timer_handle = cfdp_mem.inactivity_timer_handles[0];
    txn->retransmit_timer_handle = cfdp_mem.retransmit_timer_handles[0];

    // Timer handles were pre-created in init_cfdp() and wired into txn by
    // cfdp_alloc_transaction(). Reset the timer IDs to point to this
    // transaction now that the struct is fully initialised, then start the
    // inactivity timer.
    vTimerSetTimerID(txn->inactivity_timer_handle, (void *)txn);
    info("cfdp put: set inactivity timer handle\n");
    vTimerSetTimerID(txn->retransmit_timer_handle, (void *)txn);
    info("cfdp put: set retransmit timer handle\n");
    start_timer(txn->inactivity_timer_handle);
    info("cfdp put: started inactivty timer\n");

    info("cfdp put: complete\n");

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

    if (dir == CFDP_SEND) {
        txn->condition_code = CFDP_COND_CANCEL_REQ;
        cfdp_send_eof(txn);
    } else {
        txn->condition_code = CFDP_COND_CANCEL_REQ;
        cfdp_send_fin(txn);
    }

    if (txn->reliable_mode) {
        txn->state = (dir == CFDP_SEND) ? CFDP_SEND_STATE_WAIT_ACK : CFDP_RECV_STATE_WAIT_FIN_ACK;
        txn->ack_retransmit_counter = 0;
        start_timer(txn->retransmit_timer_handle);
    } else {
        stop_timer(txn->inactivity_timer_handle);
        stop_timer(txn->retransmit_timer_handle);

        cfdp_txn_store.active[store_index] = false;
        cfdp_txn_store.slot_free = true;
    }
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command
 * dispatcher) ---------- */

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
            cfdp_put_request(&p_cmd->data.cfdp_request->put_data);
            break;
        case OPERATION_CFDP_CANCEL:
            cfdp_cancel_request(p_cmd->data.cfdp_request->txn_id);
            break;
        case OPERATION_CFDP_TIMEOUT_RETRANSMIT:
            retransmit_timer_timeout(p_cmd->data.cfdp_timeout_data);
            break;
        case OPERATION_CFDP_TIMEOUT_INACTIVITY:
            inactivity_timer_timeout(p_cmd->data.cfdp_timeout_data);
            break;
        default:
            warning("cfdp request: invalid operation for cfdp! operation %d\n", p_cmd->operation);
    }
}

/* ---------- PDU directive handlers (called from cfdp_process_pdu) ----------
 */

static void handle_filedata_pdu(cfdp_transaction_t *txn, const uint8_t *pdu_data, size_t pdu_data_sz, bool largefile,
                                bool segment_metadata_field) {
    if (txn == NULL || txn->direction != CFDP_RECV) {
        warning("cfdp: file data pdu for unknown/non-recv txn\n");
        return;
    }

    if (txn->state == CFDP_RECV_STATE_MISSING_MD) {
        info("cfdp: disregarding file data pdu for txn w/o metadata\n");
        return;
    }

    cfdp_pdu_filedata_t fd;
    if (cfdp_pdu_filedata_parse(pdu_data, pdu_data_sz, largefile, segment_metadata_field, &fd) < 0) {
        warning("cfdp: failed to parse file data pdu\n");
        return;
    }

    if (fd.data.len == 0) {
        reset_timer(txn->inactivity_timer_handle);
        return;
    }

    bool stored = (txn->file_data != NULL) && (fd.offset <= txn->file_size) && (fd.data.len <= txn->file_size - fd.offset);

    if (stored) {
        memcpy(txn->file_data + fd.offset, fd.data.data, fd.data.len);
    } else {
        warning(
            "cfdp: file data pdu out of range for file, dropping (offset=%zu "
            "len=%zu size=%zu)\n",
            fd.offset, fd.data.len, txn->file_size);
        reset_timer(txn->inactivity_timer_handle);
        return;
    }

    info("processing fd pdu (offset=%zu len=%zu size=%zu)\n", fd.offset, fd.data.len, txn->file_size);
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
                    warning(
                        "cfdp: no room to split gap in NAK buffer, fd pdu "
                        "disregarded");
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
            warning(
                "cfdp: no room to properly update NAK buffer, therefore the fd "
                "pdu will be disregarded");
        }
    }

    reset_timer(txn->inactivity_timer_handle);
}

static void handle_eof_pdu(cfdp_transaction_t *txn, const uint8_t *pdu_data, uint16_t pdu_data_length) {
    if (txn == NULL || txn->direction != CFDP_RECV) {
        warning("cfdp: eof pdu for unknown/non-recv txn\n");
        return;
    }

    // drop if already recieved an EOF.

    cfdp_pdu_eof_t eof;
    if (cfdp_pdu_eof_parse(pdu_data, pdu_data_length, false, &eof) < 0) {
        warning("cfdp: failed to parse eof pdu\n");
        return;
    }

    if (eof.condition_code != CFDP_COND_NOERROR) {
        cfdp_send_ack(txn, CFDP_DIR_EOF, 0, eof.condition_code, 0x01);
        txn->state = CFDP_RECV_STATE_ERR;
        return;
    }

    if (txn->state == CFDP_RECV_STATE_MISSING_MD) {
        info("disregarding cond code 0 ack while missing metadata\n");
        return;
    }

    txn->file_size = eof.filesize;
    txn->expected_checksum = eof.checksum;
    reset_timer(txn->inactivity_timer_handle);

    cfdp_send_ack(txn, CFDP_DIR_EOF, 0, CFDP_COND_NOERROR, 0x01);

    // must update nak buffer to include gap from most recently recved to end.

    if (txn->eof_acked)
        return;

    txn->eof_acked = true;

    if (txn->file_offset < txn->file_size) {
        cfdp_pdu_segment_request_t req = (cfdp_pdu_segment_request_t){.start_offset = txn->file_offset, .end_offset = txn->file_size};
        cfdp_nak_buf_push(&txn->nak_buf, req);
        txn->file_offset = txn->file_size;
    }

    if (txn->reliable_mode && txn->nak_buf.size > 0) {
        txn->state = CFDP_RECV_STATE_SEND_NAK;
        txn->checksum = eof.checksum;
        start_timer(txn->retransmit_timer_handle);
    } else {
        uint32_t computed = (txn->file_data != NULL) ? cfdp_calculate_modular_checksum(txn) : 0;
        if (txn->checksum_type == 0 && computed != eof.checksum) {
            warning(
                "cfdp: checksum mismatch; computed = 0x%08lx expected = "
                "0x%08lx\n",
                computed, eof.checksum);
            txn->condition_code = CFDP_COND_FILE_CHECKSUM_FAIL;
            txn->checksum = eof.checksum;
            cfdp_send_fin(txn);
            if (!txn->reliable_mode) {
                txn->state = CFDP_RECV_STATE_ERR;
                return;
            }
            txn->state = CFDP_RECV_STATE_SEND_FIN;
            start_timer(txn->retransmit_timer_handle);
            return;
        }
        txn->checksum = eof.checksum;
        info("cfdp: checksum verified; computed = 0x%08lx expected = 0x%08lx\n", computed, eof.checksum);
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

    info("cfdp: recved fin w/ code: %d\n", fin.condition_code);

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

    if (txn->state != CFDP_RECV_STATE_WAIT_FIN_ACK && txn->state != CFDP_SEND_STATE_WAIT_ACK) {
        info("disregarding unexpected ack pdu\n");
    }

    if (pdu_data_sz < 2) {
        warning("cfdp: ack pdu too short: %d\n", pdu_data_sz);
        return;
    }

    cfdp_pdu_ack_t ack;
    cfdp_pdu_ack_parse(pdu_data, pdu_data_sz, &ack);

    stop_timer(txn->retransmit_timer_handle);
    txn->ack_retransmit_counter = 0;

    info("cfdp: eof/fin condition code: %d\n", ack.condition_code);

    if (ack.directive_code == CFDP_DIR_EOF && txn->direction == CFDP_SEND) {
        txn->state = txn->reliable_mode ? CFDP_SEND_STATE_WAIT_FIN : CFDP_SEND_STATE_DONE;
        txn->eof_acked = true;
    } else if (ack.directive_code == CFDP_DIR_FINISHED && txn->direction == CFDP_RECV) {
        if (ack.condition_code != CFDP_COND_NOERROR) {
            txn->state = CFDP_RECV_STATE_ERR;
        } else {
            txn->state = CFDP_RECV_STATE_DONE;
        }
    }

    reset_timer(txn->inactivity_timer_handle);
}

static void handle_metadata_pdu(cfdp_transaction_t *txn, const uint8_t *pdu_data, size_t pdu_data_sz) {
    // Processes metadata if it hasn't been recieved yet, otherwise disregard;
    if (txn != NULL) {
        if (txn->state != CFDP_RECV_STATE_MISSING_MD) {
            warning(
                "cfdp: duplicate metadata pdu for existing txn \xe2\x80\x94 "
                "ignored\n");
            return;
        }

        if (pdu_data_sz < 2) {
            warning("cfdp: metadata pdu too short to parse\n");
        }

        cfdp_pdu_metadata_t meta;
        if (cfdp_pdu_metadata_parse(pdu_data, pdu_data_sz, &meta) < 0) {
            warning("cfdp: failed to parse metadata\n");
            return;
        }

        uint8_t *data = NULL;

        if (meta.file_length <= CFDP_SMALL_BUFF_SZ) {
            info("cfdp: allocating small buffer for incoming txn of size %u\n", meta.file_length);
            data = cfdp_alloc_small_buff();
        } else if (meta.file_length <= CFDP_LARGE_BUFF_SZ) {
            info("cfdp: allocating large buffer for incoming txn of size %u\n", meta.file_length);
            data = cfdp_alloc_large_buff();
        } else {
            warning("cfdp: incoming file larger than supported size\n");
            return;
        }

        if (data == NULL) {
            warning("cfdp: no free buffer to allocate\n");
            return;
        }
        stop_timer(txn->retransmit_timer_handle);
        txn->state = CFDP_RECV_STATE_WAIT_EOF;
        txn->file_size = meta.file_length;
        txn->file_offset = 0;
        txn->checksum_type = meta.checksum_type;
        txn->nak_buf.head = 0;
        txn->nak_buf.tail = 0;
        txn->nak_buf.size = 0;
        txn->file_data = data;
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
    cfdp_pdu_nak_t nak;
    cfdp_pdu_nak_parse(nak_body, 8, &nak);

    if (nak.start_of_scope == 0 && nak.end_of_scope == 0) {
        info("processing metadata NAK\n");
        txn->state = CFDP_SEND_STATE_METADATA_SEND;
        reset_timer(txn->inactivity_timer_handle);
        return;
    }

    size_t nak_body_sz = pdu_data_sz;
    uint32_t seg_count = (nak_body_sz - 8) / 8;
    for (uint32_t i = 0; i < seg_count && i < CFDP_MAX_SEGMENT_REQUESTS; i++) {
        const uint8_t *s = nak_body + 8 + (i * 8);
        cfdp_pdu_segment_request_t seg;
        cfdp_pdu_segment_request_parse(s, 8, &seg);
        if (seg.start_offset == 0 && seg.end_offset == 0) {
            txn->state = CFDP_SEND_STATE_METADATA_SEND;
        } else {
            cfdp_nak_buf_push(&txn->nak_buf, seg);
        }
    }
    if (txn->eof_acked) {
        txn->state = CFDP_SEND_STATE_WAIT_FIN;
    } else {
        txn->state = CFDP_SEND_STATE_FILE_SEND;
    }

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
    int bytes_read = cfdp_pdu_header_parse(raw, sz, &header);

    if (bytes_read < 0) {
        warning("cfdp: incoming pdu of invalid size\n");
        return;
    }

    header_size = (size_t)bytes_read;
    pdu_data = raw + header_size;

    if (header.pdu_data_length > sz - header_size) {
        warning("cfdp: labeled pdu size does not match to recved len");
        return;
    }
    size_t pdu_data_sz = header.pdu_data_length;

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
        if (dir_code == CFDP_DIR_FINISHED) {
            for (size_t i = 0; i < CFDP_SAVED_TXNS; ++i) {
                if (cfdp_fin_txn.txns[i].src_entity_id == header.source_entity_id &&
                    cfdp_fin_txn.txns[i].seq_num == header.transaction_seq) {
                    cfdp_pdu_finished_t fin;
                    cfdp_pdu_finished_parse(pdu_data, pdu_data_sz, &fin);
                    cfdp_send_stale_ack(&header, fin.condition_code);
                    info("cfdp: sending stale ack for txn id: %d\n", header.transaction_seq);
                    return;
                }
            }
            warning("cfdp: txn: %d is not a stale txn\n", header.transaction_seq);
        }
        if (dir_code != CFDP_DIR_METADATA) {
            // Only request metadata when we'd be the receiver of this
            // transaction (file data or EOF arrived before metadata) and the
            // transaction is acknowledged mode. Never answer
            // ACK/NAK/Finished/Prompt/ Keep Alive for an unknown txn, or two
            // entities will NAK each other forever.
            bool is_receiver_pdu = (header.pdu_type == CFDP_PDU_TYPE_FILEDATA) || (dir_code == CFDP_DIR_EOF);
            if (is_receiver_pdu && header.transmission_mode == 0) {
                cfdp_transaction_t *new_txn = cfdp_alloc_transaction(&cfdp_txn_store);
                if (new_txn == NULL) {
                    warning("cfdp: alloc failed for new recv txn w/o metadata\n");
                    return;
                }
                new_txn->transaction_id.entity_id = header.source_entity_id;
                new_txn->transaction_id.seq_num = header.transaction_seq;
                new_txn->dest_entity_id = header.dest_entity_id;
                new_txn->state = CFDP_RECV_STATE_MISSING_MD;
                new_txn->direction = CFDP_RECV;
                new_txn->reliable_mode = (header.transmission_mode == 0);
                new_txn->ack_retransmit_counter = 0;
                new_txn->nak_retransmit_counter = 0;
                new_txn->nak_buf.head = 0;
                new_txn->nak_buf.tail = 0;
                new_txn->nak_buf.size = 0;
                new_txn->source_filename = (cfdp_lv_t){.length = 0, .value = NULL};
                new_txn->dest_filename = (cfdp_lv_t){.length = 0, .value = NULL};
                new_txn->condition_code = CFDP_COND_NOERROR;
                new_txn->eof_acked = false;
                new_txn->file_data = NULL;

                cfdp_send_metadata_nak(&header);
                cfdp_nak_buf_push(&new_txn->nak_buf, (cfdp_pdu_segment_request_t){.start_offset = 0, .end_offset = 0});

                vTimerSetTimerID(new_txn->inactivity_timer_handle, (void *)new_txn);
                vTimerSetTimerID(new_txn->retransmit_timer_handle, (void *)new_txn);
                start_timer(new_txn->inactivity_timer_handle);
                start_timer(new_txn->retransmit_timer_handle);

                info("setup up new transaction w/o metadata\n");
                return;
            }
            warning(
                "cfdp: unable to process incoming pdu of type %x from "
                "unrecognized transaction\n",
                dir_code);
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
            // need to send a fin here to show that we recieved the transaction,
            // but are not accepting it
            cfdp_send_reject_fin(&header, &meta, CFDP_COND_INVALID_TRANSMISSION);
            return;
        }

        if (meta.checksum_type != 0x00 && meta.checksum_type != 0x0F) {
            warning("cfdp: unsported checksum type, rejecting");
            cfdp_send_reject_fin(&header, &meta, CFDP_COND_BAD_CHECKSUM);
        }

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
        new_txn->reliable_mode = (header.transmission_mode == 0);
        new_txn->ack_retransmit_counter = 0;
        new_txn->nak_retransmit_counter = 0;
        new_txn->nak_buf.head = 0;
        new_txn->nak_buf.tail = 0;
        new_txn->nak_buf.size = 0;
        new_txn->source_filename = (cfdp_lv_t){.length = 0, .value = NULL};
        new_txn->dest_filename = (cfdp_lv_t){.length = 0, .value = NULL};
        new_txn->checksum_type = meta.checksum_type;
        new_txn->condition_code = CFDP_COND_NOERROR;
        new_txn->eof_acked = false;

        // Timer handles were pre-created in init_cfdp() and wired into new_txn
        // by cfdp_alloc_transaction(). Set IDs now that the struct is fully
        // initialised.
        vTimerSetTimerID(new_txn->inactivity_timer_handle, (void *)new_txn);
        vTimerSetTimerID(new_txn->retransmit_timer_handle, (void *)new_txn);
        start_timer(new_txn->inactivity_timer_handle);

        uint8_t *data = NULL;

        if (meta.file_length <= CFDP_SMALL_BUFF_SZ) {
            info("cfdp: allocating small buffer for incoming txn of size %u\n", meta.file_length);
            data = cfdp_alloc_small_buff();
        } else if (meta.file_length <= CFDP_LARGE_BUFF_SZ) {
            info("cfdp: allocating large buffer for incoming txn of size %u\n", meta.file_length);
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
        info("recved: metadata pdu\n");
        return;
    }

    info("recved: pdu of type %d\n", dir_code);
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
            handle_metadata_pdu(txn, pdu_data, pdu_data_sz);
            break;
        case CFDP_DIR_NAK:
            handle_nak_pdu(txn, pdu_data, pdu_data_sz);
            break;
        default:
            warning("cfdp: unrecognized directive code: %x", dir_code);
            return;
    }
}

/* ---------- CFDP State Machines ---------- */

static bool wait_ack_start = 0;

cfdp_result_t cfdp_handle_send_state(cfdp_transaction_t *transaction, uint32_t elapsed_ms) {
    (void)elapsed_ms;
    switch (transaction->state) {
        case CFDP_SEND_STATE_METADATA_SEND:
            wait_ack_start = 0;
            cfdp_send_metadata(transaction);
            transaction->state = CFDP_SEND_STATE_FILE_SEND;
            return CFDP_RESULT_IN_PROGRESS;
        case CFDP_SEND_STATE_FILE_SEND:
            wait_ack_start = 0;
            if (transaction->reliable_mode && transaction->nak_buf.size > 0) {
                cfdp_resend(transaction);
            } else if (transaction->file_offset < transaction->file_size) {
                uint32_t remaining = transaction->file_size - transaction->file_offset;
                uint32_t chunk_size = (remaining > SEGMENT_SIZE) ? SEGMENT_SIZE : remaining;
                info("file pdu size: %u\n", chunk_size);
                cfdp_send_filedata(transaction, transaction->file_offset, chunk_size);
                transaction->file_offset += chunk_size;
            } else {
                cfdp_send_eof(transaction);
                if (transaction->reliable_mode) {
                    transaction->state = CFDP_SEND_STATE_WAIT_ACK;
                    start_timer(transaction->retransmit_timer_handle);
                    transaction->ack_retransmit_counter = 0;
                } else {
                    transaction->state = CFDP_SEND_STATE_DONE;
                }
            }
            return CFDP_RESULT_IN_PROGRESS;
        case CFDP_SEND_STATE_WAIT_ACK:
            if (!wait_ack_start) {
                info("waiting for ack \n");
                wait_ack_start = 1;
            }
            return CFDP_RESULT_BLOCKED;
        case CFDP_SEND_STATE_WAIT_FIN:
            wait_ack_start = 0;
            if (transaction->reliable_mode && transaction->nak_buf.size > 0) {
                cfdp_resend(transaction);
            }
            return CFDP_RESULT_BLOCKED;
        case CFDP_SEND_STATE_DONE:
            wait_ack_start = 0;
            return CFDP_RESULT_COMPLETE;
        case CFDP_SEND_STATE_ERR:
            wait_ack_start = 0;
            return CFDP_RESULT_ERROR;

        default:
            wait_ack_start = 0;
            return CFDP_RESULT_ERROR;
    }
}

cfdp_result_t cfdp_handle_recv_state(cfdp_transaction_t *transaction, uint32_t elapsed_ms) {
    (void)elapsed_ms;
    switch (transaction->state) {
        case CFDP_RECV_STATE_MISSING_MD:
            return CFDP_RESULT_BLOCKED;
        case CFDP_RECV_STATE_WAIT_EOF:
            return CFDP_RESULT_BLOCKED;
        case CFDP_RECV_STATE_SEND_NAK:
            cfdp_send_nak(transaction);
            // if (transaction->nak_buf.size == 0) {
            transaction->state = CFDP_RECV_STATE_WAIT_RETRANSMIT;
            //}
            return CFDP_RESULT_IN_PROGRESS;
        case CFDP_RECV_STATE_WAIT_RETRANSMIT:
            if (transaction->nak_buf.size == 0) {
                stop_timer(transaction->retransmit_timer_handle);
                transaction->state = CFDP_RECV_STATE_SEND_FIN;
            }
            return CFDP_RESULT_BLOCKED;
        case CFDP_RECV_STATE_SEND_FIN:
            {
                uint32_t computed = (transaction->file_data != NULL) ? cfdp_calculate_modular_checksum(transaction) : 0;
                if (transaction->checksum_type == 0 && computed != transaction->checksum) {
                    warning(
                        "cfdp: checksum mismatch; computed = 0x%08lx expected = "
                        "0x%08lx\n",
                        computed, transaction->checksum);
                    transaction->condition_code = CFDP_COND_FILE_CHECKSUM_FAIL;
                }
                transaction->state = CFDP_RECV_STATE_WAIT_FIN_ACK;
                start_timer(transaction->retransmit_timer_handle);
                cfdp_send_fin(transaction);
                transaction->ack_retransmit_counter = 0;

                return CFDP_RESULT_BLOCKED;
            }
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
