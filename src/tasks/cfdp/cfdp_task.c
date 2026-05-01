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
    uint8_t *file_data = (type == IMAGE) ? IMAGE_BUF : TELEMETRY_BUF;

    cfdp_txn_store.transactions[i] = {.transaction_id = (cfdp_transaction_id_t){.entity_id = ENTITY_ID_SPACECRAFT, .seq_num = seq_num},
                                      .dest_entity_id = ENTITY_ID_SPACECRAFT,
                                      .inactivity_timer = 0,
                                      .ack_timer = 0,
                                      .nak_timer = 0,
                                      .checksum_type = 0,
                                      .eof_retransmit_counter = 0,
                                      .nak_retransmit_counter = 0,
                                      .nak_buf = (cfdp_nak_buf_t){.segments = {0}, .head = 0, .tail = 0, .size = 0},
                                      .file_size = file_size,
                                      .file_offset = 0,
                                      .state = state,
                                      .reliable_mode = true,
                                      .file_data = file_data,
                                      .source_filename = (cfdp_lv_t){.length = 4, .value = seq_num},
                                      .dest_filename = (cfdp_lv_t){.length = 4, .value = seq_num}};
}

void cfdp_cancel_request(uint32_t txn_id) {
    size_t store_index = MAX_TRANSACTIONS;
    for (size_t i = 0; i < MAX_TRANSACTIONS; ++i) {
        if (cfdp_txn_store.active[i] && cfdp_txn_store.transactions[i].transaction_id == txn_id) {}
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
    int bytes_read = cfdp_pdu_header_parse(raw, sz, &header);

    if (bytes_read < 0) {
        debug("cfdp: incoming pdu of invalid size\n");
        return;
    }

    size_t header_sz = (size_t)bytes_read;

    // get PDU type
    uint8_t dir_code = 0;

    if (header.pdu_type == 0) {
        dir_code = raw[header_sz];
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
            debug("cfdp: incoming from unknown sequence %x pdu of type %x disregarded\n", header.transaction_seq, dir_code);
            return;
        }

        if (!cfdp_txn_store.slot_free) {
            debug("cfdp: txn store full, unable to accept sequence: %x, from entity: %x\n", header.transaction_seq,
                  header.source_entity_id);
            return;
        }

        // create new transaction
    }

    size_t dir_sz = (dir_code) ? 1 : 0;
    size_t crc_sz = (header.crc) ? 2 : 0;
    if (header_sz + header.pdu_data_length + dir_sz + crc_sz > sz) { // should this check for minimum bound or exact?
        debug("cfdp: incoming pdu below specified length\n");
    }

    if (header.crc && cfdp_process_crc(raw, header_sz + header.pdu_data_length + dir_sz) != 0) {
        debug("cfdp: crc failed, incoming pdu disregarded\n");
    }

    uint8_t *pdu = raw + header_sz + dir_sz;

    // Note: probably want to strucutre this where each case calls a function that handles that PDU type specifically
    switch (dir_code) {
        case 0: // Not an offical CFDP Directive Code, but we will treat it as file data
            break;
        case CFDP_DIR_EOF:
            cfdp_pdu_eof_t eof;
            cfdp_pdu_eof_parse(pdu, header.pdu_data_length, false, &eof);

            if (txn->direction != CFDP_RECV) {
                debug("cfdp: recieved eof for a transaction we are sending, pdu disregarded\n");
                return;
            }

            cfdp_update_nak_buf(txn, txn->file_size); // need to work out how this will be done

            // I think we can assume all files we recieve will not use variable size, so we can disregard the new size data here
            // not sure though
            if (eof.condition_code != CFDP_COND_NOERROR) {
                debug("cfdp: error code recieved in incoming eof pdu: %x\n", header.transaction_seq);
                txn->state = CFDP_RECV_STATE_ERR;
                return;
            }

            txn->checksum = eof.checksum; // calculate once we have recievied entire file.

            if (txn->nak_buf.size > 0) {
                txn->state = CFDP_RECV_STATE_SEND_NAK;
            } else {
                txn->state = CFDP_RECV_STATE_SEND_FIN;
            }

            break;
        case CFDP_DIR_FINISHED:
            cfdp_pdu_finished_t fin;
            cfdp_pdu_finished_parse(pdu, header.pdu_data_length, &fin);

            if (txn->direction != CFDP_SEND) {
                debug("cfdp: recieved fin for a transaction we are receiving, pdu disregarded\n");
                return;
            }

            if (fin.condition_code != CFDP_COND_NOERROR || fin.delivery_code == 1) {
                debug("cfdp: error condition/delivery code recieved in incoming fin pdu: %x\n", header.transaction_seq);
                txn->state = CFDP_SEND_STATE_ERR;
                return;
            }
            // if we add support for filestore interaction, that will need to be dealt with here.

            txn->state = CFDP_SEND_STATE_DONE;
            break;
        case CFDP_DIR_ACK:
            cfdp_pdu_ack_t ack;
            cfdp_pdu_ack_parse(pdu, header.pdu_data_length, &ack);

            if (txn->direction == CFDP_SEND && ack.directive_code == CFDP_DIR_EOF) {
                debug("cfdp: recieved an eof ack for a transaction we are transmitting, pdu disregarded\n");
                return;
            }

            if (txn->direction == CFDP_RECV && ack.directive_code == CFDP_DIR_FINISHED) {
                debug("cfdp: recieved an fin ack for a transaction we are recieving, pdu disregarded\n");
                return;
            }

            if (ack.directive_code == CFDP_DIR_EOF) {
                if (ack.condition_code != CFDP_COND_NOERROR) {
                    debug("");
                }
            }

            break;
        case CFDP_DIR_METADATA: // should always be handled with the check above, because we should never recieve metadata for a txn we
                                // already have in store
            break;
        case CFDP_DIR_NAK:
            break;
        default:
            debug("cfdp: unrecognized directive code: %x", dir_code);
            return;
    }
}