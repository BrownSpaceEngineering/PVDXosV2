/**
 * arducam_main.c
 *
 * Main loop of the Arducam task which controls the Arducam OV2640 camera.
 *
 * Created: November 17, 2024
 * Authors: Alexander Thaep, Tanish Makadia, Zach Mahan
 */

#include "arducam_driver.h"
#include "arducam_task.h"

// Arducam Task memory structures
arducam_task_memory_t arducam_mem;

/**
 * \fn arducam_main
 *
 * \param pvParameters a void pointer to the parameters required by the arducam;
 *      not currently set by config
 *
 * \warning should never return
 */
void main_arducam(void *pvParameters) {
    info("arducam: Task Started!\n");

    // Initialize the arducam hardware
    init_arducam();

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time the command dispatcher should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;

    while (true) {
        debug_impl("\n---------- Arducam Task Loop ----------\n");

        // Execute all commands contained in the queue
        if (xQueueReceive(p_arducam_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            do {
                debug("arducam: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                // exec_command_arducam(&cmd); // TODO: implement exec_command_arducam in arducam_task.c
            } while (xQueueReceive(p_arducam_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("arducam: No more commands queued.\n");

        // TODO: Add any periodic tasks here (e.g. capture image periodically if needed)
        // For now, we just wait for commands.

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("arducam: Enqueued watchdog checkin command\n");
        }
    }
}

