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

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;

    // Initialize the arducam hardware
    status_t status = init_arducam_hardware();
    fatal_on_error(status, "Failed to initialize arducam hardware!\n");

    // How long to wait between captures. Kept below the watchdog timeout/2 so the
    // task still checks in on time (arducam watchdog_timeout_ms is 10000).
    const TickType_t capture_interval_ticks = pdMS_TO_TICKS(3000);

    while (true) {
        debug_impl("\n---------- Arducam Task Loop ----------\n");

        // Take a picture and stream it over RTT (channel 2)
        info("arducam: Capturing image...\n");
        capture_rtt();

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("arducam: Enqueued watchdog checkin command\n");
        }

        // Wait ~3s before the next capture, draining any queued commands meanwhile
        if (xQueueReceive(p_arducam_task->command_queue, &cmd, capture_interval_ticks) == pdPASS) {
            do {
                debug("arducam: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                // exec_command_arducam(&cmd); // TODO: implement exec_command_arducam in arducam_task.c
            } while (xQueueReceive(p_arducam_task->command_queue, &cmd, 0) == pdPASS);
        }
    }
}

