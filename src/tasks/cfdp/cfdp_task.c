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

size_t cfdp_put_request(cfdp_txn_type_t type, cfdp_direction_t dir) {
    size_t store_index = MAX_TRANSACTIONS;
    for (size_t i = 0; i < MAX_TRANSACTIONS; i++) {
        if (!cfdp_txn_store.active[i]) {
            store_index = i;
            break;
        }
    }
    if (store_index == MAX_TRANSACTIONS) {
        fatal("cfdp put: no valid transaction slots\n");
    }

    cfdp_txn_store.active[store_index] = true;

    cfdp_txn_store.slot_free = false;
    for (size_t i = 0; i < MAX_TRANSACTIONS; i++) {
        if (!cfdp_txn_store.active[i]) {
            cfdp_txn_store.slot_free = true;
            break;
        }
    }

    size_t file_size = (type == IMAGE) ? IMAGE_FILE_SZ : TELEMETRY_FILE_SZ;
    cfdp_state_t state = (dir == CFDP_SEND) ? CFDP_SEND_STATE_METADATA_SEND : CFDP_RECV_STATE_FILE_RECV;
    uint32_t seq_num = next_seq_num();
    uint8_t *file_data = (type == IMAGE) ? (uint8_t *)IMAGE_BUF : (uint8_t *)TELEMETRY_BUF;

    cfdp_transaction_t *txn = &cfdp_txn_store.transactions[store_index];
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

    return store_index;
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

    // not sure yet how we're gonna handle errors.
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
                  header.source_entity_id);
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
        new_txn->transaction_id.seq_num   = header.transaction_seq;
        new_txn->dest_entity_id           = header.dest_entity_id;
        new_txn->file_size                = meta.file_length;
        new_txn->file_offset              = 0;
        new_txn->state                    = CFDP_RECV_STATE_FILE_RECV;
        new_txn->direction                = CFDP_RECV;
        new_txn->reliable_mode            = (meta.closure_req != 0);
        new_txn->inactivity_timer         = 0;
        new_txn->ack_timer                = 0;
        new_txn->nak_timer                = 0;
        new_txn->eof_retransmit_counter   = 0;
        new_txn->nak_retransmit_counter   = 0;
        new_txn->nak_buf.head             = 0;
        new_txn->nak_buf.tail             = 0;
        new_txn->nak_buf.size             = 0;
        memset(new_txn->received_bitmap, 0, sizeof(new_txn->received_bitmap));
        new_txn->file_data                = NULL; // caller must wire up a receive buffer
        new_txn->source_filename          = (cfdp_lv_t){.length = 0, .value = NULL};
        new_txn->dest_filename            = (cfdp_lv_t){.length = 0, .value = NULL};
        new_txn->radio_handle             = NULL; // not needed for recv side
        txn = new_txn;
        // Fall through into the switch — dir_code == CFDP_DIR_METADATA will hit the duplicate-guard and return cleanly.
    }

    // Note: probably want to structure this where each case calls a function that handles that PDU type specifically
    switch (dir_code) {
        case 0: {
            // Not an official CFDP Directive Code — treat as file data (pdu_type == CFDP_FILE_DATA).
            // File data PDUs have no directive byte; pdu_data points directly at the offset + payload.
            if (txn == NULL || txn->direction != CFDP_RECV) {
                debug("cfdp: file data pdu for unknown/non-recv txn\n");
                return;
            }
            cfdp_pdu_filedata_t fd;
            if (cfdp_pdu_filedata_parse(pdu_data, pdu_data_sz, header.largefile, header.segment_metadata_field, &fd) < 0) {
                debug("cfdp: failed to parse file data pdu\n");
                return;
            }
            // Write the received bytes into the file buffer at the signalled offset.
            if (txn->file_data != NULL && fd.offset + fd.data.len <= txn->file_size) {
                memcpy(txn->file_data + fd.offset, fd.data.data, fd.data.len);
            }
            // Mark every segment covered by this PDU as received in the bitmap.
            // A single file-data PDU may span multiple SEGMENT_SIZE chunks if the sender
            // used a larger-than-SEGMENT_SIZE payload (e.g. during retransmit).
            {
                uint32_t first_seg = fd.offset / SEGMENT_SIZE;
                uint32_t last_seg  = (fd.offset + (fd.data.len > 0 ? fd.data.len - 1 : 0)) / SEGMENT_SIZE;
                for (uint32_t s = first_seg; s <= last_seg && s < CFDP_MAX_SEGMENTS; s++) {
                    txn->received_bitmap[s / 32] |= (1u << (s % 32));
                }
            }
            // Advance file_offset to track the highest contiguous byte received.
            if (fd.offset + fd.data.len > txn->file_offset) {
                txn->file_offset = fd.offset + fd.data.len;
            }
            txn->inactivity_timer = 0; // reset inactivity timer on any received data
            break;
        }
        case CFDP_DIR_EOF: {
            // Sender signals end-of-file. Receiver verifies checksum and flags any missing segments.
            if (txn == NULL || txn->direction != CFDP_RECV) {
                debug("cfdp: eof pdu for unknown/non-recv txn\n");
                return;
            }
            // Skip the directive code byte before parsing the EOF body.
            cfdp_pdu_eof_t eof;
            if (cfdp_pdu_eof_parse(pdu_data + 1, pdu_data_sz - 1, header.largefile, &eof) < 0) {
                debug("cfdp: failed to parse eof pdu\n");
                return;
            }
            txn->file_size = eof.filesize; // ground truth file size from sender
            txn->inactivity_timer = 0;

            if (eof.condition_code != CFDP_COND_NOERROR) {
                // Sender cancelled — abort the transaction.
                txn->state = CFDP_RECV_STATE_ERR;
                return;
            }

            // Scan the received-bitmap for missing segments and queue NAK requests.
            // Each bit represents one SEGMENT_SIZE-byte chunk; 0 = not yet received.
            txn->nak_buf.head = 0;
            txn->nak_buf.tail = 0;
            txn->nak_buf.size = 0;
            uint32_t total_segments = (txn->file_size + SEGMENT_SIZE - 1) / SEGMENT_SIZE;
            for (uint32_t seg_idx = 0; seg_idx < total_segments && seg_idx < CFDP_MAX_SEGMENTS; seg_idx++) {
                bool received = (txn->received_bitmap[seg_idx / 32] & (1u << (seg_idx % 32))) != 0;
                if (!received) {
                    uint32_t start = seg_idx * SEGMENT_SIZE;
                    uint32_t end   = start + SEGMENT_SIZE;
                    if (end > txn->file_size) {
                        end = txn->file_size;
                    }
                    cfdp_pdu_segment_request_t seg = {.start_offset = start, .end_offset = end};
                    cfdp_nak_buf_push(&txn->nak_buf, seg);
                }
            }

            if (txn->nak_buf.size > 0) {
                // There are gaps — request retransmission.
                txn->state = CFDP_RECV_STATE_SEND_NAK;
            } else {
                // File is complete — verify the checksum.
                // Build a temporary transaction-like context to reuse the checksum helper.
                uint32_t computed = 0;
                if (txn->file_data != NULL) {
                    // Inline modular checksum over the receive buffer.
                    size_t words = txn->file_size / 4;
                    for (uint32_t i = 0; i < words * 4; i += 4) {
                        computed += ((uint32_t)txn->file_data[i] << 24) | ((uint32_t)txn->file_data[i + 1] << 16) |
                                    ((uint32_t)txn->file_data[i + 2] << 8) | txn->file_data[i + 3];
                    }
                    size_t rem = txn->file_size % 4;
                    uint32_t tail_word = 0;
                    for (uint32_t i = 0; i < rem; i++) {
                        tail_word |= (uint32_t)txn->file_data[words * 4 + i] << (8 * (3 - i));
                    }
                    computed += tail_word;
                }
                if (computed != eof.checksum) {
                    debug("cfdp: checksum mismatch — computed 0x%08lx expected 0x%08lx\n", computed, eof.checksum);
                    txn->state = CFDP_RECV_STATE_ERR;
                    return;
                }
                // All data received and checksum passes.
                if (txn->reliable_mode) {
                    txn->state = CFDP_RECV_STATE_SEND_FIN; // send Finished PDU to close the loop
                } else {
                    txn->state = CFDP_RECV_STATE_DONE;
                }
            }
            break;
        }
        case CFDP_DIR_FINISHED: {
            // Receiver has confirmed the transfer is complete (reliable mode, send side).
            if (txn == NULL || txn->direction != CFDP_SEND) {
                debug("cfdp: finished pdu for unknown/non-send txn\n");
                return;
            }
            // Complete the Class-2 closeout by ACKing the received Finished PDU.
            // directive_subtype_code is unused for Finished ACK in this implementation.
            cfdp_send_ack(txn, CFDP_DIR_FINISHED, 0, CFDP_COND_NOERROR, 0x02);
            txn->state = CFDP_SEND_STATE_DONE;
            txn->inactivity_timer = 0;
            break;
        }
        case CFDP_DIR_ACK: {
            // Acknowledgement of an EOF or FIN PDU — stop the retransmit timer.
            if (txn == NULL) {
                debug("cfdp: ack pdu for unknown txn\n");
                return;
            }
            // Skip directive code byte before parsing ACK body.
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
                // ACK of our EOF — move from WAIT_ACK to waiting for FIN (reliable) or done.
                txn->ack_timer = 0;
                txn->nak_timer = 0;
                txn->eof_retransmit_counter = 0;
                txn->state = txn->reliable_mode ? CFDP_SEND_STATE_WAIT_FIN : CFDP_SEND_STATE_DONE;
            } else if (ack.directive_code == CFDP_DIR_FINISHED && txn->direction == CFDP_RECV) {
                // Sender acknowledged our Finished PDU; receiver side is now complete.
                txn->ack_timer = 0;
                txn->state = CFDP_RECV_STATE_DONE;
            }
            txn->inactivity_timer = 0;
            break;
        }
        case CFDP_DIR_METADATA: {
            // Should always be handled by the new-transaction creation above.
            // If txn is NULL here it means the store was full; that was already logged above.
            if (txn != NULL) {
                debug("cfdp: duplicate metadata pdu for existing txn — ignored\n");
            }
            break;
        }
        case CFDP_DIR_NAK: {
            // Receiver is telling us which segments it is missing; queue them for retransmit.
            if (txn == NULL || txn->direction != CFDP_SEND) {
                debug("cfdp: nak pdu for unknown/non-send txn\n");
                return;
            }
            // Skip directive code byte, then parse start_of_scope (4), end_of_scope (4), segment list.
            if (pdu_data_sz < 9) {
                debug("cfdp: nak pdu too short\n");
                return;
            }
            const uint8_t *nak_body = pdu_data + 1; // skip directive code
            size_t nak_body_sz = pdu_data_sz - 1;
            uint32_t seg_count = (nak_body_sz - 8) / 8; // each segment request is 8 bytes
            for (uint32_t i = 0; i < seg_count && i < CFDP_MAX_SEGMENT_REQUESTS; i++) {
                const uint8_t *s = nak_body + 8 + (i * 8);
                cfdp_pdu_segment_request_t seg;
                seg.start_offset = ((uint32_t)s[0] << 24) | ((uint32_t)s[1] << 16) | ((uint32_t)s[2] << 8) | s[3];
                seg.end_offset = ((uint32_t)s[4] << 24) | ((uint32_t)s[5] << 16) | ((uint32_t)s[6] << 8) | s[7];
                cfdp_nak_buf_push(&txn->nak_buf, seg);
            }
            // Trigger the send-side state machine to retransmit.
            txn->state = CFDP_SEND_STATE_FILE_SEND;
            txn->inactivity_timer = 0;
            break;
        }
        default:
            debug("cfdp: unrecognized directive code: %x", dir_code);
            return;
    }
}