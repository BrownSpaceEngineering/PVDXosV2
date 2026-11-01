#include "cfdp_timer.h"

#include "cfdp_task.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"

// --------- Timer Init (Ran in CFDP Task) ---------
void init_cfdp_timers(cfdp_task_memory_t *mem) {
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        mem->inactivity_timer_handles[i] = xTimerCreateStatic("Inactivity Timer", pdMS_TO_TICKS(TRANSACTION_LIFETIME_MS),
                                                              pdFALSE, // one-shot
                                                              NULL,    // ID set per-transaction after alloc
                                                              inactivity_timer_callback, &cfdp_mem.inactivity_timer_mem[i]);

        mem->retransmit_timer_handles[i] = xTimerCreateStatic("Retransmit Timer", // covers both ACK-wait and NAK-wait roles
                                                              pdMS_TO_TICKS(RETRANSMIT_TIMEOUT_MS), // ACK_TIMEOUT_MS == NAK_TIMEOUT_MS
                                                              pdTRUE, // auto-reload (periodic, stopped explicitly when done)
                                                              NULL, retransmit_timer_callback, &cfdp_mem.retransmit_timer_mem[i]);
    }
}

// --------- Timer Timeout Functions (Ran in CFDP Task) ---------

// A timeout command can still be queued after its transaction is freed, since freeing stops the timer but can't recall a
// command it already sent. By then the slot may be free or hold a new transaction, which seq_num tells apart
static cfdp_transaction_t *timeout_txn(const cfdp_timeout_t *timeout) {
    for (size_t i = 0; i < MAX_TRANSACTIONS; i++) {
        if (&cfdp_txn_store.transactions[i] == timeout->txn) {
            if (cfdp_txn_store.active[i] && timeout->txn->transaction_id.seq_num == timeout->seq_num) {
                return timeout->txn;
            }
            return NULL;
        }
    }
    return NULL;
}

void inactivity_timer_timeout(const cfdp_timeout_t *timeout) {
    cfdp_transaction_t *txn = timeout_txn(timeout);
    if (txn == NULL) {
        warning("timer: inactivity timeout for finished txn %lu, ignoring\n", timeout->seq_num);
        return;
    }
    stop_timer(txn->retransmit_timer_handle);
    txn->condition_code = CFDP_COND_INACTIVITY;
    if (txn->direction == CFDP_SEND) {
        txn->state = CFDP_SEND_STATE_ERR;
        cfdp_send_eof(txn);
    } else {
        txn->state = CFDP_RECV_STATE_ERR;
        txn->condition_code = CFDP_COND_INACTIVITY;
        cfdp_send_fin(txn);
    }
}

void retransmit_timer_timeout(const cfdp_timeout_t *timeout) {
    cfdp_transaction_t *txn = timeout_txn(timeout);
    if (txn == NULL) {
        warning("timer: retransmit timeout for finished txn %lu, ignoring\n", timeout->seq_num);
        return;
    }
    // --- ACK role (was ack_timer_callback) ---
    if (txn->state == CFDP_SEND_STATE_WAIT_ACK || txn->state == CFDP_RECV_STATE_WAIT_FIN_ACK) {
        if (txn->ack_retransmit_counter >= ACK_RETRANSMIT_LIMIT) {
            if (xTimerStop(txn->retransmit_timer_handle, 0) != pdPASS) {
                warning("timer: unable to stop retransmit timer even though ACK retransmit limit reached\n");
                return; // timer double counts, but it gives time for the timer queue to process
            }

            txn->state = (txn->direction == CFDP_SEND) ? CFDP_SEND_STATE_ERR : CFDP_RECV_STATE_ERR;
            txn->condition_code = CFDP_COND_ACK_LIMIT;
        }

        if (txn->direction == CFDP_SEND) { // EOF-ACK retransmit
            cfdp_send_eof(txn);
        } else { // FIN-ACK retransmit
            cfdp_send_fin(txn);
        }

        txn->ack_retransmit_counter++;
        return;
    }

    if (txn->direction == CFDP_SEND) {
        warning("timer: retransmit timer fired for a transaction in unexpected state, attempting to stop\n");
        xTimerStop(txn->retransmit_timer_handle, 0);
        return;
    }

    // --- NAK role (was nak_timer_callback) ---

    if (txn->nak_retransmit_counter >= NAK_RETRANSMIT_LIMIT) {
        txn->condition_code = CFDP_COND_NAK_LIMIT;
        cfdp_send_fin(txn);

        if (xTimerStart(txn->retransmit_timer_handle, 0) != pdPASS) {
            warning("timer: unable to restart retransmit timer for FIN-ACK wait after NAK limit\n");
        }

        txn->state = CFDP_RECV_STATE_WAIT_FIN_ACK;
        txn->ack_retransmit_counter = 0;
        return;
    }

    cfdp_send_nak(txn);
    txn->nak_retransmit_counter++;
}

// --------- Timer Callback Functions (Ran in FreeRTOS Timer Task) ---------
void inactivity_timer_callback(TimerHandle_t timer) {
    cfdp_transaction_t *txn = (cfdp_transaction_t *)pvTimerGetTimerID(timer);
    command_t cmd = (command_t){.target = p_cfdp_task,
                                .data.cfdp_timeout = {.txn = txn, .seq_num = txn->transaction_id.seq_num},
                                .data_type = CMD_CFDP_TIMEOUT,
                                .operation = OPERATION_CFDP_TIMEOUT_INACTIVITY,
                                .result = NO_STATUS_RETURN};
    enqueue_command(&cmd);
}

void retransmit_timer_callback(TimerHandle_t timer) {
    cfdp_transaction_t *txn = (cfdp_transaction_t *)pvTimerGetTimerID(timer);
    command_t cmd = (command_t){.target = p_cfdp_task,
                                .data.cfdp_timeout = {.txn = txn, .seq_num = txn->transaction_id.seq_num},
                                .data_type = CMD_CFDP_TIMEOUT,
                                .operation = OPERATION_CFDP_TIMEOUT_RETRANSMIT,
                                .result = NO_STATUS_RETURN};
    enqueue_command(&cmd);
}
