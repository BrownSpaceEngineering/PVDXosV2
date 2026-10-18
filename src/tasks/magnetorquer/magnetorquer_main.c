/**
 * magnetorquer_main.c
 *
 * Main loop of the magnetorquer task: brings up the three DRV8837 H-bridges (via TCC0/TCC1
 * PWM) and leaves all axes stopped. It does NOT actuate uncommanded - a future ADCS command
 * path will drive mtq_set(). See magnetorquer_driver.h.
 *
 * Created: October 2026
 * Authors: PVDX
 */

#include "magnetorquer_task.h"
#include "magnetorquer_driver.h"

// Magnetorquer task memory structures
magnetorquer_task_memory_t magnetorquer_mem;

/**
 * \fn main_magnetorquer
 *
 * \param pvParameters a void pointer to the parameters required by the magnetorquer task;
 *      not currently set by config
 *
 * \warning should never return
 */
void main_magnetorquer(void *pvParameters) {
    info("magnetorquer: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);

    // init enables the TCC timers and leaves all three axes coasting (duty 0, drivers asleep).
    status_t status = init_magnetorquers();
    if (status != SUCCESS) {
        warning("magnetorquer: init_magnetorquers() failed (status=%d)\n", status);
    } else {
        info("magnetorquer: initialized (all axes stopped)\n");
    }

    const TickType_t interval_ticks = pdMS_TO_TICKS(2000);

    while (true) {
        debug("\n---------- Magnetorquer Task Loop ----------\n");

        // Do not fire actuators uncommanded; just report the last-commanded duties
        // (milli-units to avoid relying on %f in printf).
        info("magnetorquer: duty(milli) x=%ld y=%ld z=%ld\n", (long)(mtq_get_duty(MTQ_X) * 1000.0f),
             (long)(mtq_get_duty(MTQ_Y) * 1000.0f), (long)(mtq_get_duty(MTQ_Z) * 1000.0f));

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("magnetorquer: Enqueued watchdog checkin command\n");
        }

        vTaskDelay(interval_ticks);
    }
}
