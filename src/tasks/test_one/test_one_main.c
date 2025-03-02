/**
 * test_one_main.c
 *
 * Created: February 24, 2025
 * Authors:
 */

#include "test_one_task.h"

test_one_task_memory_t test_one_mem;

void main_test_one(void *pvParameters) {
    info("test_one: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time this task should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;

    while (true) {
        debug_impl("\n---------- test_one Task Loop ----------\n");

        // Block waiting for at least one command to appear in the command queue
        if (xQueueReceive(p_test_one_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            // Once there is at least one command in the queue, empty the entire queue
            do {
                debug("test_one: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);

                switch (cmd.operation) {
                    case TEST_OP:
                        handle_cmd_test_one(&cmd);
                        break;

                    default:
                        fatal("test_one: operation %d not supported", cmd.operation);
                        break;
                }

            } while (xQueueReceive(p_test_one_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("test_one: No more commands queued.\n");

        // Check in with the watchdog task
        ;if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
        }
        debug("test_one: Enqueued watchdog checkin command\n");

        // Wait 1 second before attempting to run the loop again
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
