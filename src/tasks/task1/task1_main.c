/**
 * task1_main.c
 *
 * Created: March 20, 2025
 * Authors:
 */

#include "task1_task.h"

task1_task_memory_t task1_mem;

/**
 * \fn main_task1
 *
 * \param pvParameters a void pointer to the parametres required by task1; not currently set by config
 *
 * \warning should never return
 */
void main_task1(void *pvParameters) {
    info("task1: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time this task should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;

    while (true) {
        debug_impl("\n---------- task1 Task Loop ----------\n");

        // Block waiting for at least one command to appear in the command queue
        if (xQueueReceive(p_test_one_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            // Once there is at least one command in the queue, empty the entire queue
            do {
                debug("task1: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_task1(&cmd);

            } while (xQueueReceive(p_test_one_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("task1: No more commands queued.\n");

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
        }
        debug("task1: Enqueued watchdog checkin command\n");

        // Wait 1 second before attempting to run the loop again
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
