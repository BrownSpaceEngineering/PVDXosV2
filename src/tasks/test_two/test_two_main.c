/**
 * test_two_main.c
 *
 * Created: February 24, 2025
 * Authors:
 */

#include "test_two_task.h"

test_two_task_memory_t test_two_mem;

char test_two_message[20] = "test 2 says hi!";

void main_test_two(void *pvParameters) {
    info("test_two: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time this task should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;

    command_t test_two_cmd = {
        .target = p_test_one_task,
        .operation = TEST_OP,
        .p_data = test_two_message,
        .len = 20,
        .result = PROCESSING,
        .callback = NULL,
    };

    enqueue_command(&test_two_cmd);

    while (true) {
        debug_impl("\n---------- test_two Task Loop ----------\n");

        // Block waiting for at least one command to appear in the command queue
        if (xQueueReceive(p_test_two_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            // Once there is at least one command in the queue, empty the entire queue
            do {
                debug("test_two: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);

                switch (cmd.operation) {
                    case TEST_OP:
                        handle_cmd_test_two(&cmd);
                        break;

                    default:
                        fatal("test_two: operation %d not supported", cmd.operation);
                        break;
                }

            } while (xQueueReceive(p_test_two_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("test_two: No more commands queued.\n");

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
        }
        debug("test_two: Enqueued watchdog checkin command\n");
        if (test_two_cmd.result != SUCCESS) {
            enqueue_command(&test_two_cmd);
        }

        // Wait 1 second before attempting to run the loop again
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
