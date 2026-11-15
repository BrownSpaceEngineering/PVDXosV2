#include "cfdp_timer.h"

#include "cfdp_task.h"
#include "logging.h"
#include "task_list.h"

// --------- Timer Init (Ran in CFDP Task) ---------
void init_cfdp_timers(cfdp_task_memory_t *mem) {
    mem->inactivity_timer_handle = xTimerCreateStatic("Inactivity Timer", pdMS_TO_TICKS(TRANSACTION_LIFETIME_MS),
                                                      pdFALSE, // one-shot
                                                      NULL, inactivity_timer_callback, &mem->inactivity_timer_mem);

    if (mem->inactivity_timer_handle == NULL) {
        fatal("cfdp: failed to create inactivity timer\n");
    }
}

// --------- Timeout Functions (Ran in CFDP Task) ---------

// Starting a transaction clears pending timeout notifications, so this acts on the active transaction. It can still
// run with none active if it finished in the same pass the timer fired
void inactivity_timer_timeout(void) {
    if (!cfdp_txn_store.active) {
        warning("timer: inactivity timeout with no active txn, ignoring\n");
        return;
    }
    cfdp_transaction_t *txn = &cfdp_txn_store.txn;

    disarm_retransmit(txn);
    txn->condition_code = CFDP_COND_INACTIVITY;
    if (txn->direction == CFDP_SEND) {
        txn->state = CFDP_SEND_STATE_ERR;
        cfdp_send_eof(txn);
    } else {
        txn->state = CFDP_RECV_STATE_ERR;
        cfdp_send_fin(txn);
    }
}

/**
 * Called by the main loop at the start of our turn once the active transaction's retransmit_turn is reached: the ground
 * has had a whole turn to answer and hasn't. Deferred NAK: everything we send goes out in our turn and the ground's
 * answers come back in its turn, so this runs at most once per turn.
 */
void cfdp_turn_retransmit(void) {
    if (!cfdp_txn_store.active) {
        return;
    }
    cfdp_transaction_t *txn = &cfdp_txn_store.txn;

    // --- ACK role: the EOF or FIN wasn't acknowledged ---
    if (txn->state == CFDP_SEND_STATE_WAIT_ACK || txn->state == CFDP_RECV_STATE_WAIT_FIN_ACK) {
        if (txn->ack_retransmit_counter >= ACK_RETRANSMIT_LIMIT) {
            warning("cfdp: no ACK after %u turns, giving up on txn %lu\n", txn->ack_retransmit_counter, txn->transaction_id.seq_num);
            disarm_retransmit(txn);
            txn->state = (txn->direction == CFDP_SEND) ? CFDP_SEND_STATE_ERR : CFDP_RECV_STATE_ERR;
            txn->condition_code = CFDP_COND_ACK_LIMIT;
            return;
        }

        if (txn->direction == CFDP_SEND) {
            cfdp_send_eof(txn);
        } else {
            cfdp_send_fin(txn);
        }
        txn->ack_retransmit_counter++;
        arm_retransmit(txn);
        return;
    }

    if (txn->direction == CFDP_SEND) {
        warning("cfdp: retransmit armed in unexpected send state %d, disarming\n", txn->state);
        disarm_retransmit(txn);
        return;
    }

    // --- NAK role: gaps (or the metadata) are still missing after the ground's turn ---
    if (txn->nak_retransmit_counter >= NAK_RETRANSMIT_LIMIT) {
        txn->condition_code = CFDP_COND_NAK_LIMIT;
        cfdp_send_fin(txn);
        txn->state = CFDP_RECV_STATE_WAIT_FIN_ACK;
        txn->ack_retransmit_counter = 0;
        arm_retransmit(txn);
        return;
    }

    cfdp_send_nak(txn);
    txn->nak_retransmit_counter++;
    arm_retransmit(txn);
}

// --------- Timer Callback Functions (Ran in FreeRTOS Timer Task) ---------
void inactivity_timer_callback(TimerHandle_t timer) {
    (void)timer;
    xTaskNotify(p_cfdp_task->handle, CFDP_NOTIFY_INACTIVITY, eSetBits);
}
