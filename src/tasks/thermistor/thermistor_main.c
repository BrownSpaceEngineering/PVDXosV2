/**
 * thermistor_main.c
 *
 * Main loop of the thermistor task: reads the four NTC thermistors, logs temperatures,
 * and runs the fault-tolerant closed-loop heater control each cycle. See
 * thermistor_driver.h and THERMISTOR_FAULT_PLAN.md.
 *
 * Created: October 2026
 * Authors: PVDX
 */

#include "thermistor_task.h"
#include "thermistor_driver.h"

// Thermistor task memory structures
thermistor_task_memory_t thermistor_mem;

/**
 * \fn main_thermistor
 *
 * \param pvParameters a void pointer to the parameters required by the thermistor task;
 *      not currently set by config
 *
 * \warning should never return
 */
void main_thermistor(void *pvParameters) {
    info("thermistor: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);

    // Configure ADC1 channels + heater enable lines (driven off)
    status_t status = init_thermistors();
    if (status != SUCCESS) {
        warning("thermistor: init_thermistors() failed (status=%d)\n", status);
    } else {
        info("thermistor: initialized\n");
    }

    // Kept below the watchdog timeout/2 so the task still checks in on time.
    const TickType_t interval_ticks = pdMS_TO_TICKS(2000);

    while (true) {
        debug("\n---------- Thermistor Task Loop ----------\n");

        float temps[THERM_COUNT];
        status_t rs = therm_read_all(temps);
        if (rs != SUCCESS) {
            warning("thermistor: therm_read_all reported a fault (status=%d)\n", rs);
        }
        // Temperatures printed as integer milli-degrees to avoid relying on %f in printf.
        for (uint8_t i = 0; i < THERM_COUNT; i++) {
            info("thermistor: T%u = %ld mC\n", i, (long)(temps[i] * 1000.0f));
        }

        // Fault-tolerant bang-bang heater control (pair-partner / consensus replacement).
        heater_control_update();
        info("thermistor: heater1=%s heater2=%s\n", heater_is_on(0) ? "ON" : "OFF",
             heater_is_on(1) ? "ON" : "OFF");

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("thermistor: Enqueued watchdog checkin command\n");
        }

        vTaskDelay(interval_ticks);
    }
}
