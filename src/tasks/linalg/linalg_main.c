/**
 * linalg_main.c
 *
 * Created: February 08, 2026
 * Authors:
 */

#include "linalg_task.h"

#define row_a 1
#define row_c 1
#define column_b 1
#define L 20


linalg_task_memory_t linalg_mem;

/**
 * \fn main_linalg
 *
 * \param pvParameters a void pointer to the parametres required by linalg; not currently set by config
 *
 * \warning should never return
 */
void main_linalg(void *pvParameters) {
	
	// Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time the command dispatcher should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;

    while (true) {
        debug_impl("\n---------- linalg Task Loop ----------\n");

        // Block waiting for at least one command to appear in the command queue
        if (xQueueReceive(p_linalg_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            // Once there is at least one command in the queue, empty the entire queue
            do {
                debug("linalg: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_linalg(&cmd); 
                // TODO: implement contents of main loop. 

            } while (xQueueReceive(p_linalg_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("linalg: No more commands queued.\n");

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
        }
        debug("linalg: Enqueued watchdog checkin command\n");
    }
}
