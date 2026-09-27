/**
 * magnetometer_main.c
 *
 * Main loop of the magnetometer task which reads the RM3100 over SPI.
 *
 * Created: September 2026
 * Authors: PVDX
 */

#include "magnetometer_driver.h"
#include "magnetometer_task.h"

// Magnetometer Task memory structures
magnetometer_task_memory_t magnetometer_mem;

/**
 * \fn main_magnetometer
 *
 * \param pvParameters a void pointer to the parameters required by the magnetometer;
 *      not currently set by config
 *
 * \warning should never return
 */
void main_magnetometer(void *pvParameters) {
    info("magnetometer: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Variable to hold commands popped off the queue
    command_t cmd;

    // Initialize the RM3100 (SPI bring-up + continuous measurement mode)
    status_t status = init_rm3100();
    if (status != SUCCESS) {
        warning("magnetometer: init_rm3100() failed (status=%d)\n", status);
    } else {
        info("magnetometer: RM3100 initialized (continuous mode)\n");
    }

    // How long to wait between reads. Kept below the watchdog timeout/2 so the
    // task still checks in on time (watchdog_timeout_ms is 10000).
    const TickType_t read_interval_ticks = pdMS_TO_TICKS(2000);

    while (true) {
        debug_impl("\n---------- Magnetometer Task Loop ----------\n");

        int32_t raw[3];
        float adj[3];
        status_t rs = magnetometer_read(raw, adj);
        if (rs == SUCCESS) {
            // Raw counts always print; gain-adjusted values are floats, so log
            // their truncated integer part to avoid relying on %f in the printf.
            info("magnetometer: raw x=%ld y=%ld z=%ld | adj(int) x=%ld y=%ld z=%ld\n", (long)raw[0], (long)raw[1],
                 (long)raw[2], (long)adj[0], (long)adj[1], (long)adj[2]);
        } else if (rs == ERROR_NOT_READY) {
            debug("magnetometer: data not ready this cycle\n");
        } else {
            warning("magnetometer: read failed (status=%d)\n", rs);
        }

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("magnetometer: Enqueued watchdog checkin command\n");
        }

        // Wait before the next read, draining any queued commands meanwhile
        if (xQueueReceive(p_magnetometer_task->command_queue, &cmd, read_interval_ticks) == pdPASS) {
            do {
                debug("magnetometer: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
            } while (xQueueReceive(p_magnetometer_task->command_queue, &cmd, 0) == pdPASS);
        }
    }
}
