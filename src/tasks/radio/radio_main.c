/**
 * radio_main.c
 *
 *
 * Created: October 7, 2026
 * Modified: October 7, 2026
 * Authors: Noah Shepard
 */

#include "ccsds/uslp.h"
#include "cmd_dispatcher.h"
#include "logging.h"
#include "radio_task.h"
#include "watchdog_task.h"

// Radio Task memory structures
radio_task_memory_t radio_mem;
QueueHandle_t downlink_queue;

/**
 * \fn init_radio
 *
 * \brief Initialises radio command queue, before `init_task_pointer()`.
 *
 * \returns QueueHandle_t, a handle to the created queue
 *
 * \see `init_task_pointer()` for usage of functions of the type `init_<TASK>()`
 */
QueueHandle_t init_radio(void) {
    QueueHandle_t radio_command_queue_handle = xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE,
                                                                  radio_mem.radio_command_queue_buffer, &radio_mem.radio_task_queue);

    if (radio_command_queue_handle == NULL) {
        fatal("Failed to create radio command queue!\n");
    }

    downlink_queue = xQueueCreateStatic(RADIO_DOWNLINK_QUEUE_LENGTH, sizeof(radio_downlink_item_t), radio_mem.downlink_queue_buffer,
                                        &radio_mem.downlink_queue_mem);

    if (downlink_queue == NULL) {
        fatal("Failed to create radio downlink queue!\n");
    }

    return radio_command_queue_handle;
}

/**
 * \fn main_radio
 *
 * \param pvParameters a void pointer to the parameters required by radio functions; not currently set by config
 *
 * \warning should never return
 */
void main_radio(void *pvParameters) {
    info("radio: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time this task should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Variable to hold commands popped off the queue
    command_t cmd;
    radio_downlink_item_t item;

    while (true) {
        // Block waiting for at least one command to appear in the command queue
        if (xQueueReceive(p_radio_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            // Once there is at least one command in the queue, empty the entire queue
            info("Radio: performing command\n");
        }
        debug("Radio: No more commands queued.\n");

        if (xQueueReceive(downlink_queue, &item, queue_block_time_ticks) == pdPASS) {
            if (uslp_mapp_request(item.data, item.len, RADIO_GMAP_ID, SPP_VERSION_NUMBER, RADIO_SDU_ID, USLP_QOS_EXPEDITED)) {
                warning("Radio: failed to send packet");
            }
        }

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("Radio: Enqueued watchdog checkin command\n");
        }
    }
}
