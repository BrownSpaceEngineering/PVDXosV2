/**
 * display_main.c
 *
 * Main loop of the Display task which controls the OLED display on PVDX.
 *
 * Created: February 29, 2024
 * Authors: Tanish Makadia, Ignacio Blancas Rodriguez, Siddharta Laloux
 */

#include "command_dispatcher_task.h"
#include "display_task.h"

// Display Task memory structures
display_task_memory_t display_mem;

/**
 * \fn main_display
 *
 * \param pvParameters a void pointer to the parametres required by the display;
 *      not currently set by config
 *
 * \warning should never return
 */
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

    // Initialize the display hardware
    status_t status = init_display_hardware();

    fatal_on_error(status, "Failed to initialize display hardware!\n");

    while (true) {
        debug("\n---------- Display Task Loop ----------\n");

        // Execute all commands contained in the queue
        if (xQueueReceive(p_display_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            do {
                debug("display: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_display(&cmd);
            } while (xQueueReceive(p_display_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("display: No more commands queued.\n");

        // Enqueue image display commands. Commands are copied by value into the queue, so
        // .result cannot be checked here (it reflects only the pre-enqueue PROCESSING state).
        // TODO: add a blocking/callback mechanism to observe the per-command result.
        {
            command_t display_image_command = get_display_image_command(IMAGE_BUFFER_PVDX);
            enqueue_command(&display_image_command);
        }
        {
            command_t display_image_command = get_display_image_command(IMAGE_BUFFER_BROWNLOGO);
            enqueue_command(&display_image_command);
        }

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("display: Enqueued watchdog checkin command\n");
        }
    }
}
