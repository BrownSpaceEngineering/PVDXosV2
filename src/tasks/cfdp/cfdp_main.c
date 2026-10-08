/**
 * cfdp_main.c
 *
 * Main loop of the CFDP Engine, which handles state of active transactions
 *
 * Created: April 24, 2026
 * Modified: September 30, 2026
 * Authors: Noah Shepard
 */

#include "cfdp_task.h"
#include "cfdp_timer.h"
#include "cfdp_utils.h"
#include "command_dispatcher_task.h"
#include "globals.h"
#include "logging.h"
#include "watchdog_task.h"

// Radio Task Memory Structure & CFDP Memory Stores
cfdp_task_memory_t radio_mem;
cfdp_transaction_store_t cfdp_txn_store;
cfdp_large_buff_t cfdp_large_buff;
cfdp_small_buffs_t cfdp_small_buffs;
fin_txn_buf_t cfdp_fin_txn;

QueueHandle_t radio_uplink_queue;
QueueHandle_t radio_downlink_queue;

StaticQueue_t radio_uplink_queue_mem;
StaticQueue_t radio_downlink_queue_mem;

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

    cfdp_fin_txn = (fin_txn_buf_t){0};

    return cfdp_command_queue_handle;
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
    // Cache the watchdog checkin command to avoid creating it every iteration

    command_t cmd_checkin = get_watchdog_checkin_command(current_task);

    // Calculate the maximum time this task should block (and thus be unable to
    // check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Variable to hold commands popped off the queue
    command_t cmd;
    uint8_t recv_buff[CFDP_MAX_PDU_SIZE];

    cfdp_txn_store.slot_free = true;
    for (size_t i = 0; i < MAX_TRANSACTIONS; ++i) {
        cfdp_txn_store.active[i] = false;
    }

    while (true) {
        while (cfdp_txn_store.slot_free && xQueueReceive(current_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            info("cfdp: performing command\n");
            exec_command_cfdp_request(&cmd);
            info("cfdp: finished command\n");
        }

        // handle CFDP State
        if (cfdp_recv(recv_buff, TXN_FRAME) == 0 /* Whatever success status code is*/) {
            info("cfdp: recieved PDU\n");
            cfdp_process_pdu(recv_buff, TXN_FRAME);
        }
        uint32_t elapsed_ms = 10; // how do i use RTC? / calculate this

        for (size_t i = 0; i < MAX_TRANSACTIONS; i++) {
            if (!cfdp_txn_store.active[i]) {
                continue;
            }

            // probably need to update elapsed_ms for each transaction
            cfdp_result_t result;
            do {
                result = cfdp_transact(&cfdp_txn_store.transactions[i], elapsed_ms);
            } while (result == CFDP_RESULT_IN_PROGRESS);

            if (result == CFDP_RESULT_COMPLETE) {
                info("cfdp: transaction %lu complete\n", cfdp_txn_store.transactions[i].transaction_id.seq_num);
                if (cfdp_txn_store.transactions[i].direction == CFDP_SEND) {
                    info("cfdp: adding tracking for stale txn with seq: %d\n", cfdp_txn_store.transactions[i].transaction_id.seq_num);
                    fin_txn_buf_push(&cfdp_fin_txn,
                                     (fin_txn_t){.seq_num = cfdp_txn_store.transactions[i].transaction_id.seq_num,
                                                 .src_entity_id = cfdp_txn_store.transactions[i].transaction_id.entity_id});
                }

                cfdp_free_transaction(&cfdp_txn_store, &cfdp_txn_store.transactions[i]);
            } else if (result == CFDP_RESULT_ERROR) {
                warning("cfdp: transaction %lu ended with error\n", cfdp_txn_store.transactions[i].transaction_id.seq_num);
                cfdp_free_transaction(&cfdp_txn_store, &cfdp_txn_store.transactions[i]);
            }
        }
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("cfdp: Enqueued watchdog checkin command\n");
        }
    }
}
