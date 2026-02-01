/**
 * photodiode_main.c
 *
 * Main loop of the Photodiode task which handles sun sensing for ADCS.
 *
 * Created: September 20, 2025
 * Authors: Avinash Patel, Yi Lyo
 */

#include "photodiode_task.h"

// Photodiode Task memory structures
photodiode_task_memory_t photodiode_mem;

/**
 * \fn main_photodiode
 *
 * \param pvParameters a void pointer to the parametres required by photodiode; not currently set by config
 *
 * \warning should never return
 */
void main_photodiode(void *pvParameters) {
    info("photodiode: Task Started!\n");

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
        if (xQueueReceive(p_photodiode_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            // Once there is at least one command in the queue, empty the entire queue
            do {
                debug("photodiode: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_photodiode(&cmd);

            } while (xQueueReceive(p_photodiode_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("photodiode: No more commands queued.\n");

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("photodiode: Enqueued watchdog checkin command\n");
        }
    }
}
