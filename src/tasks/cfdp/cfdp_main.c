/**
 * cfdp_main.c
 *
 * Created: October 30, 2025
 * Authors: Siddharta Laloux
 */

#include "cfdp_task.h"

cfdp_task_memory_t cfdp_mem;

/**
 * \fn main_cfdp
 *
 * \param pvParameters a void pointer to the parametres required by cfdp; not currently set by config
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
    // Varible to hold commands popped off the queue
    command_t cmd;

    while (true) {
        debug_impl("\n---------- cfdp Task Loop ----------\n");

        // Block waiting for at least one command to appear in the command queue
        if (xQueueReceive(p_cfdp_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            // Once there is at least one command in the queue, empty the entire queue
            do {
                debug("cfdp: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_cfdp(&cmd); 
                // TODO: implement contents of main loop. 

            } while (xQueueReceive(p_cfdp_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("cfdp: No more commands queued.\n");

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
        }
        debug("cfdp: Enqueued watchdog checkin command\n");
    }
}
