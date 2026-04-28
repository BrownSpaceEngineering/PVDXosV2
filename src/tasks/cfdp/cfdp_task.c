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
    size_t store_index = MAX_TRANSACTIONS;
    for (size_t i = 0; i < MAX_TRANSACTIONS; i++) {
        if (!cfdp_txn_store.active[i]) {
            store_index = i;
            break;
        }
    }
    if (store_index == MAX_TRANSACTIONS) {
        fatal("cfdp put: no valid transaction slots");
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
        debug("cfdp cancel: txn_id: %lu not active", txn_id);
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