/**
 * display_main.c
 *
 * Main loop of the Display task which controls the OLED display on PVDX.
 *
 * Created: February 29, 2024
 * Authors: Tanish Makadia, Ignacio Blancas Rodriguez, Siddharta Laloux
 */

#include "display_task.h"

// Display Task memory structures
display_task_memory_t display_mem;

void main_display(void *pvParameters) {
    info("display: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time the command dispatcher should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;

    while (true) {
        debug_impl("\n---------- Display Task Loop ----------\n");

        // Execute all commands contained in the queue
        if (xQueueReceive(p_display_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            do {
                debug("display: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_display(&cmd);
            } while (xQueueReceive(p_display_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("display: No more commands queued.\n");

        // TODO: is this a correct modification?
        // Set the display buffer to the first image
        status_t result = SUCCESS; // TODO: Don't initialize result to SUCCESS and block until it is set by the display_image command
        {
            // TODO: Add logic for blocking on the result of the display_image command
            command_t display_image_command = get_display_image_command(IMAGE_BUFFER_PVDX);
            enqueue_command(&display_image_command);

            if (display_image_command.result != SUCCESS) {
                warning("display: Failed to display image. Error code: %d\n", result);
            }
        }
        {
            // Set the display buffer to the second image
            // TODO: Add logic for blocking on the result of the display_image command
            command_t display_image_command = get_display_image_command(IMAGE_BUFFER_BROWNLOGO);
            enqueue_command(&display_image_command);

            if (display_image_command.result != SUCCESS) {
                warning("display: Failed to display image. Error code: %d\n", result);
            }
        }
        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
        }
        debug("display: Enqueued watchdog checkin command\n");
    }
}
