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

// transaction store

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

    while (true) {
        debug_impl("\n---------- CFDP Run ----------\n");

        // Block waiting for at least one command to appear in the command queue
        if (xQueueReceive(p_cfdp_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            info("cfdp: performing command\n");
            do {
                switch (cmd.operation) {
                    case OPERATION_CFDP_REQ:
                        switch (cmd.data.cfdp_request->type) {
                            case CFDP_PUT_REQ:
                                debug("cfdp: Put Request Recieved");
                                // cfdp_put_request(cmd.data.cfdp_request->data.txn_type);
                                break;
                            case CFDP_CANCEL_REQ:
                                debug("cfdp: Cancel Request Recieved");
                                // cfdp_cancel_request(cmd.data.cfdp_request->data.txn_id);
                                break;
                            default:
                                debug("cfdp: Invalid Request Type");
                        }
                        break;
                    default:
                        debug("cfdp: Invalid CFDP Operation Type");
                        break;
                }
            } while (xQueueReceive(p_cfdp_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("cfdp: No more commands queued.\n");

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("cfdp: Enqueued watchdog checkin command\n");
        }
    }
}