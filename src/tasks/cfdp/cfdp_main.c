/**
 * cfdp_main.c
 *
 * Main loop of the CFDP Engine, which handles state of active transactions
 *
 * Created: April 24, 2026
 * Modified: April 24, 2026
 * Authors: Noah Shepard
 */

#include "cfdp_task.h"
#include "command_dispatcher_task.h"
#include "drivers/rtc/rtc_driver.h"
#include "globals.h"
#include "logging.h"
#include "watchdog_task.h"

// CFDP Task Memory Structure
cfdp_task_memory_t cfdp_mem;
cfdp_transaction_store_t cfdp_txn_store;

/**
 * \fn init_cfdp
 *
 * \brief Initialises CFDP command queue and rtc timers, before `init_task_pointer()`.
 *
 * \returns QueueHandle_t, a handle to the created queue
 *
 * \see `init_task_pointer()` for usage of functions of the type `init_<TASK>()`
 */
QueueHandle_t init_cfdp(void) {
    QueueHandle_t cfdp_command_queue_handle = xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE,
                                                                 cfdp_mem.cfdp_command_queue_buffer, &cfdp_mem.cfdp_task_queue);

    if (cfdp_command_queue_handle == NULL) {
        fatal("Failed to create cfdp command queue!\n");
    }

    // Initialize RTC timer hardware
    info("Initializing RTC timer\n");
    status_t result = init_rtc_hardware(); // i think this needs to change if more than one task is now using the timers... (double init
                                           // will reset timers)
    if (result != SUCCESS) {
        warning("cfdp rtc timer: Hardware initialization failed\n");
    }

    return cfdp_command_queue_handle;
}

/**
 * \fn main_cfdp
 *
 * \param pvParameters a void pointer to the parameters required by CFDP functions; not currently set by config
 *
 * \warning should never return
 */
void main_cfdp(void *pvParameters) {
    info("cfdp: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time this task should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Variable to hold commands popped off the queue
    command_t cmd;
    uint8_t *recv_buff;

    while (true) {
        while (cfdp_txn_store.slot_free && xQueueReceive(p_cfdp_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            info("cfdp: performing command\n");
            exec_command_cfdp_request(&cmd);
        }

        // handle CFDP State
        if (recv(recv_buff, TXN_FRAME) == 0 /* Whatever success status code is*/) {
            cfdp_process_pdu(recv_buff, TXN_FRAME);
        }
        uint32_t elapsed_ms = 10; // how do i use RTC? / calculate this

        for (size_t i = 0; i < MAX_TRANSACTIONS; i++) {
            // probably need to update elapsed_ms for each transaction
            cfdp_transact(cfdp_txn_store.transactions[i], elapsed_ms);
        }

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("cfdp: Enqueued watchdog checkin command\n");
        }
    }
}