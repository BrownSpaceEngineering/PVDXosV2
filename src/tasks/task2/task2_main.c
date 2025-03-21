/**
 * task2_main.c
 *
 * Created: March 20, 2025
 * Authors:
 */

#include "task2_task.h"

task2_task_memory_t task2_mem;

/**
 * \fn main_task2
 *
 * \param pvParameters a void pointer to the parametres required by task2; not currently set by config
 *
 * \warning should never return
 */
void main_task2(void *pvParameters) {
    info("task2: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time this task should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;

    while (true) {
        debug_impl("\n---------- task2 Task Loop ----------\n");

        // Block waiting for at least one command to appear in the command queue
        if (xQueueReceive(p_test_two_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            // Once there is at least one command in the queue, empty the entire queue
            do {
                debug("task2: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_task2(&cmd);
                // TODO: implement contents of main loop. 

            } while (xQueueReceive(p_test_two_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("task2: No more commands queued.\n");

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
        }
        debug("task2: Enqueued watchdog checkin command\n");

        // Wait 1 second before attempting to run the loop again
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
