/**
 * adcs_main.c
 *
 * Main loop of the ADCS task which handles sun sensing for ADCS and RTC timer
 *
 * Created: September 20, 2025
 * Modified: November 9, 2025
 * Authors: Avinash Patel, Yi Lyo, Alexander Thaep
 */

#include "adcs_task.h"

// ADCS Task memory structures
adcs_task_memory_t adcs_mem;

/**
 * \fn init_adcs
 *
 * \brief Initialises adcs command queue, before `init_task_pointer()`.
 *
 * \returns QueueHandle_t, a handle to the created queue
 *
 * \see `init_task_pointer()` for usage of functions of the type `init_<TASK>()`
 */
QueueHandle_t init_adcs(void) {
    QueueHandle_t adcs_command_queue_handle = xQueueCreateStatic(
        COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, adcs_mem.adcs_command_queue_buffer,
        &adcs_mem.adcs_task_queue);

    if (adcs_command_queue_handle == NULL) {
        fatal("Failed to create adcs command queue!\n");
    }

    // Initialize photodiode hardware
    status_t result = init_photodiode_hardware();
    if (result != SUCCESS) {
        warning("photodiode: Hardware initialization failed\n");
    }

    // Initialize RTC timer hardware
    info("Initializing RTC timer\n");
    result = init_rtc_hardware();
    if (result != SUCCESS) {
        warning("rtc timer: Hardware initialization failed\n");
    }

    return adcs_command_queue_handle;
}

/**
 * \fn main_adcs
 *
 * \param pvParameters a void pointer to the parametres required by photodiode; not currently set by config
 *
 * \warning should never return
 */
void main_adcs(void *pvParameters) {
    info("adcs: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time this task should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Variable to hold commands popped off the queue
    command_t cmd;

    info("photodiode: Initialized with %d photodiodes\n", PHOTODIODE_COUNT);

    while (true) {
        debug("\n---------- Photodiode Task Loop ----------\n");

        // Block waiting for at least one command to appear in the command queue
        if (xQueueReceive(p_adcs_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            // Once there is at least one command in the queue, empty the entire queue
            do {
                debug("photodiode: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_photodiode(&cmd);

            } while (xQueueReceive(p_adcs_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("photodiode: No more commands queued.\n");

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("photodiode: Enqueued watchdog checkin command\n");
        }
    }
}
