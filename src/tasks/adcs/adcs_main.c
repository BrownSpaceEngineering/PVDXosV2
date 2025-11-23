/**
 * adcs_main.c
 * 
 * Main loop of the satellite's ADCS (Attitude Determination and Control System) RTOS task
 *
 * Created: Nov 23, 2025
 * Authors: Yi Lyo
 **/

#include "adcs_task.h"

// ADCS Task memory structures
adcs_task_memory_t adcs_mem;

/**
 * \fn main_adcs
 *
 * \param pvParameters a void pointer to the parameters required by the 
 *      ADCS task; not currently set by config
 *
 * \warning should never return
 */
void main_adcs(void *pvParameters) {
    info("adcs: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time the command dispatcher should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Variable to hold commands popped off the queue
    command_t cmd;

    while (true) {
        debug_impl("\n---------- ADCS Task Loop ----------\n");

        // Execute all commands contained in the queue
        if (xQueueReceive(p_adcs_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            do {
                debug("adcs: Command popped off queue. Target: %s, Operation: %d\n", cmd.target->name, cmd.operation);
                exec_command_adcs(&cmd);
            } while (xQueueReceive(p_adcs_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("adcs: No more commands queued.\n");

        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("adcs: Enqueued watchdog checkin command\n");
        }
    }
}
