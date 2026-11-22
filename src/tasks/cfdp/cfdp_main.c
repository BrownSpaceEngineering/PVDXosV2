/**
 * cfdp_main.c
 *
 * Main loop of the CFDP Engine, which handles state of active transactions
 *
 * Created: April 24, 2026
 * Modified: September 30, 2026
 * Authors: Noah Shepard
 */

#include "ccsds/radio_routing.h"
#include "cfdp_task.h"
#include "cfdp_timer.h"
#include "cfdp_utils.h"
#include "globals.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
#include "os_delay.h"
#include "semphr.h"
#include "task_list.h"
#include "uhf_radio/uhf_radio_task.h"
#include "uplink_commands.h"
#include "watchdog_task.h"

// How long the task manager waits for the CFDP task to park. An S-band bring-up step can block for up to 2 s before it
// sees the request (see radio_sband_service())
#define CFDP_PARK_TIMEOUT_MS 3000

// CFDP Memory Stores. PDUs go to and from the radio through its uplink_queue/downlink_queue (see cfdp_utils.c)
cfdp_task_memory_t cfdp_mem;
cfdp_transaction_store_t cfdp_txn_store;
cfdp_large_buff_t cfdp_large_buff;
cfdp_small_buff_t cfdp_small_buffs[CFDP_SMALL_BUFF_COUNT];
fin_txn_buf_t cfdp_fin_txn;

// Safe-suspend handshake, as in the perovskite task
static volatile bool stop_requested = false;
static StaticSemaphore_t stop_ack_storage;
static SemaphoreHandle_t stop_ack;

// cfdp_recv receives a whole radio_queue_item_t into the buffer it's given
_Static_assert(CFDP_MAX_PDU_SIZE >= sizeof(radio_queue_item_t), "CFDP receive buffer must hold a radio_queue_item_t");

static void init_cfdp_buff(cfdp_buff_header_t *header, uint8_t *buff, bool is_large) {
    header->ref_count = 0;
    header->is_large = is_large;
    header->buff = buff;
    header->semaphore = xSemaphoreCreateMutexStatic(&header->_semaphore_mem);

    if (header->semaphore == NULL) {
        fatal("Failed to create cfdp buffer mutex!\n");
    }
}

/**
 * \fn init_cfdp
 *
 * \brief Initialises CFDP command queue and rtc timers, before
 * `init_task_pointer()`.
 *
 * \returns QueueHandle_t, a handle to the created queue
 *
 * \see `init_task_pointer()` for usage of functions of the type `init_<TASK>()`
 */
QueueHandle_t init_cfdp(void) {
    QueueHandle_t cfdp_command_queue_handle = xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE,
                                                                 cfdp_mem.cfdp_command_queue_buffer, &cfdp_mem.cfdp_task_queue);

    if (cfdp_command_queue_handle == NULL) {
        // fatal("Failed to create cfdp command queue!\n");
    }

    init_cfdp_timers(&cfdp_mem);

    for (size_t i = 0; i < CFDP_SMALL_BUFF_COUNT; i++) {
        init_cfdp_buff(&cfdp_small_buffs[i].header, cfdp_small_buffs[i].buff, false);
    }
    init_cfdp_buff(&cfdp_large_buff.header, cfdp_large_buff.buff, true);

    cfdp_fin_txn = (fin_txn_buf_t){0};

    stop_ack = xSemaphoreCreateBinaryStatic(&stop_ack_storage);
    if (stop_ack == NULL) {
        fatal("cfdp: failed to create suspend acknowledgement semaphore\n");
    }

    return cfdp_command_queue_handle;
}

bool cfdp_stop_requested(void) {
    return stop_requested;
}

bool cfdp_prepare_suspend(void) {
    if (stop_ack == NULL) {
        return false;
    }
    // Drain the previous handshake before publishing a new request
    (void)xSemaphoreTake(stop_ack, 0);
    stop_requested = true;
    return xSemaphoreTake(stop_ack, pdMS_TO_TICKS(CFDP_PARK_TIMEOUT_MS)) == pdTRUE;
}

void cfdp_prepare_resume(void) {
    // Drop late queue writes from producers that passed admission before disable
    xQueueReset(p_cfdp_task->command_queue);
    stop_requested = false;
}

void cfdp_checkin(void) {
    command_t cmd_checkin = get_watchdog_checkin_command(p_cfdp_task);
    if (should_checkin(p_cfdp_task) && enqueue_command(&cmd_checkin) != SUCCESS) {
        warning("cfdp: failed to enqueue watchdog checkin command\n");
    }
}

/**
 * Parks the task if the task manager is suspending it (leaving a pass state): drops the transaction, powers S-band off
 * and acknowledges, then waits to be suspended. Only called at the top of the main loop, where nothing is half sent
 */
static void cfdp_park_if_stopped(void) {
    if (!stop_requested) {
        return;
    }
    if (cfdp_txn_store.active) {
        warning("cfdp: leaving the pass state, dropping txn %lu\n", cfdp_txn_store.txn.transaction_id.seq_num);
        cfdp_free_transaction();
    }
    radio_sband_close();
    xQueueReset(p_cfdp_task->command_queue);
    xSemaphoreGive(stop_ack);
    // Never self-suspend: the manager could resume us just before a self-suspend
    while (stop_requested) {
        os_delay_ms(1);
    }
}

/**
 * \fn main_cfdp
 *
 * \param pvParameters a void pointer to the parameters required by CFDP
 * functions; not currently set by config
 *
 * \warning should never return
 */
void main_cfdp(void *pvParameters) {
    (void)pvParameters;
    info("cfdp: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();

    // Variable to hold commands popped off the queue
    command_t cmd;
    uint8_t recv_buff[CFDP_MAX_PDU_SIZE];

    cfdp_txn_store = (cfdp_transaction_store_t){0};

    while (true) {
        cfdp_park_if_stopped();

        // Taken before reading the uplink queue, so a PDU or turn start after this isn't counted as handled (see
        // radio_routing.h)
        const radio_link_view_t view = radio_link_view();

        // A transaction only lives as long as its pass. Nothing can be sent with the link down, so it's just dropped
        if (cfdp_txn_store.active && (!view.link_up || cfdp_txn_store.txn.pass != view.pass)) {
            warning("cfdp: pass ended, dropping txn %lu\n", cfdp_txn_store.txn.transaction_id.seq_num);
            cfdp_free_transaction();
        }

        // S-band streams for the whole of an S-band pass. Brought up here (blocking) before any transaction starts
        radio_sband_service(&view);
        // A transaction that couldn't send on S-band is killed, not moved to UHF: the ground never yields its UHF turn on
        // an S-band pass, so it would never hear it
        if (radio_sband_take_failure() && cfdp_txn_store.active) {
            warning("cfdp: S-band failed, killing txn %lu\n", cfdp_txn_store.txn.transaction_id.seq_num);
            cfdp_free_transaction();
        }
        // On an S-band pass, puts wait in the queue until the stream is up
        const bool can_send = view.link_up && (!view.sband || radio_sband_ready());
        // Notifications are only read while a transaction is active, so a cancel (e.g. the ground disabling transmit)
        // or timeout that arrives as the link goes down would otherwise hit the next pass's transaction
        if (!view.link_up) {
            xTaskNotifyWait(0, CFDP_NOTIFY_ALL, NULL, 0);
        }

        TickType_t poll = pdMS_TO_TICKS(CFDP_POLL_MS);
        if (!cfdp_txn_store.active) {
            // Only start a new transaction once the last one has finished, and only during a pass. Take one command at
            // a time: a put starts a transaction, and the rest of the queue waits until it's done
            if (!can_send) {
                vTaskDelay(poll);
            } else if (xQueueReceive(current_task->command_queue, &cmd, poll) == pdPASS) {
                radio_cfdp_set_busy();
                exec_command_cfdp_request(&cmd);
            }
        } else {
            // Timeouts and cancels arrive as notifications while a transaction is active
            uint32_t events = 0;
            if (xTaskNotifyWait(0, CFDP_NOTIFY_ALL, &events, poll) == pdTRUE) {
                radio_cfdp_set_busy();
                if (events & CFDP_NOTIFY_CANCEL) {
                    cfdp_cancel_request();
                }
                if (events & CFDP_NOTIFY_INACTIVITY) {
                    inactivity_timer_timeout();
                }
            }
        }

        // Re-send once the ground has had a full turn to answer and hasn't
        if (cfdp_txn_store.active && cfdp_txn_store.txn.retransmit_armed &&
            (int32_t)(view.turns - cfdp_txn_store.txn.retransmit_turn) >= 0) {
            radio_cfdp_set_busy();
            cfdp_turn_retransmit();
        }

        // handle every PDU the radio has received. Checks the link now rather than view, which predates the wait: the
        // queue is emptied when a pass starts, so a PDU in it is from this pass if the link is up
        const bool link_up = radio_link_view().link_up;
        int pdu_len;
        uint8_t vcid;
        while ((pdu_len = cfdp_recv(recv_buff, sizeof(recv_buff), &vcid)) != 0) {
            if (pdu_len < 0) {
                warning("cfdp: dropping malformed uplink packet\n");
                continue;
            }
            if (!link_up) {
                warning("cfdp: dropping uplink packet from a pass that has ended\n");
                continue;
            }
            info("cfdp: recieved PDU\n");
            cfdp_process_pdu(recv_buff, (size_t)pdu_len, vcid);
        }

        cfdp_result_t result = CFDP_RESULT_COMPLETE;
        if (cfdp_txn_store.active) {
            cfdp_transaction_t *txn = &cfdp_txn_store.txn;

            // Runs until the transaction is waiting on a received PDU, the downlink queue is full, or it has finished
            do {
                result = cfdp_transact(txn);
            } while (result == CFDP_RESULT_IN_PROGRESS);

            if (radio_sband_take_failure()) {
                warning("cfdp: S-band failed, killing txn %lu\n", txn->transaction_id.seq_num);
                cfdp_free_transaction();
            } else if (result == CFDP_RESULT_COMPLETE) {
                info("cfdp: transaction %lu complete\n", txn->transaction_id.seq_num);
                // The ground only sends us uplink command files, so every file we finish receiving is one
                if (txn->direction == CFDP_RECV && txn->condition_code == CFDP_COND_NOERROR) {
                    uplink_commands_dispatch(txn->buff_header, txn->file_size);
                }
                // Only class 2 sends get a FIN from the ground, so only they can get a stale one
                if (txn->direction == CFDP_SEND && txn->reliable_mode) {
                    info("cfdp: adding tracking for stale txn with seq: %d\n", txn->transaction_id.seq_num);
                    fin_txn_buf_push(&cfdp_fin_txn,
                                     (fin_txn_t){.seq_num = txn->transaction_id.seq_num, .src_entity_id = txn->transaction_id.entity_id});
                }
                if (txn->direction == CFDP_SEND) {
                    cfdp_send_complete(txn);
                }

                cfdp_free_transaction();
            } else if (result == CFDP_RESULT_ERROR) {
                warning("cfdp: transaction %lu ended with error\n", txn->transaction_id.seq_num);
                cfdp_free_transaction();
            }
        }
        // Done for this turn once nothing is left to send: a transaction waiting on the ground, or no transaction and no
        // put waiting to start one. The UHF task yields the turn then
        bool done = cfdp_txn_store.active ? (result == CFDP_RESULT_BLOCKED) : (uxQueueMessagesWaiting(current_task->command_queue) == 0);
        if (done) {
            radio_cfdp_set_done(view.inputs);
        } else {
            radio_cfdp_set_busy();
        }

        cfdp_checkin();
    }
}
