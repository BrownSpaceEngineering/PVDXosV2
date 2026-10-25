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
#include "globals.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
#include "telemetry/telemetry_timer.h"
#include "uhf_radio/uhf_radio_task.h"
#include "watchdog_task.h"

// CFDP Memory Stores. PDUs go to and from the radio through its uplink_queue/downlink_queue (see cfdp_utils.c)
cfdp_task_memory_t cfdp_mem;
cfdp_transaction_store_t cfdp_txn_store;
cfdp_large_buff_t cfdp_large_buff;
cfdp_small_buff_t cfdp_small_buffs[CFDP_SMALL_BUFF_COUNT];
fin_txn_buf_t cfdp_fin_txn;

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
    init_telemetry_timer();

    for (size_t i = 0; i < CFDP_SMALL_BUFF_COUNT; i++) {
        init_cfdp_buff(&cfdp_small_buffs[i].header, cfdp_small_buffs[i].buff, false);
    }
    init_cfdp_buff(&cfdp_large_buff.header, cfdp_large_buff.buff, true);

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

    // Variable to hold commands popped off the queue
    command_t cmd;
    uint8_t recv_buff[CFDP_MAX_PDU_SIZE];

    cfdp_txn_store.slot_free = true;
    for (size_t i = 0; i < MAX_TRANSACTIONS; ++i) {
        cfdp_txn_store.active[i] = false;
    }

    start_telemetry_timer();

    while (true) {
        // Wait briefly for the first command, then drain the rest. Timer timeouts arrive as commands too, so the queue is
        // drained even when every transaction slot is taken; a put with no free slot is dropped by cfdp_put_request
        TickType_t command_wait = pdMS_TO_TICKS(CFDP_POLL_MS);
        while (xQueueReceive(current_task->command_queue, &cmd, command_wait) == pdPASS) {
            info("cfdp: performing command\n");
            exec_command_cfdp_request(&cmd);
            info("cfdp: finished command\n");
            command_wait = 0;
        }

        // handle every PDU the radio has received
        int pdu_len;
        uint8_t vcid;
        while ((pdu_len = cfdp_recv(recv_buff, sizeof(recv_buff), &vcid)) != 0) {
            if (pdu_len < 0) {
                warning("cfdp: dropping malformed uplink packet\n");
                continue;
            }
            info("cfdp: recieved PDU\n");
            cfdp_process_pdu(recv_buff, (size_t)pdu_len, vcid);
        }
        uint32_t elapsed_ms = 10; // how do i use RTC? / calculate this

        for (size_t i = 0; i < MAX_TRANSACTIONS; i++) {
            if (!cfdp_txn_store.active[i]) {
                continue;
            }

            // probably need to update elapsed_ms for each transaction
            // Runs until the transaction is waiting on a received PDU, the downlink queue is full, or it has finished
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
