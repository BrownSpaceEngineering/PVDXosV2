/**
 * fuel_gauge_main.c
 *
 * Main loop of the fuel gauge task: reads all four MAX17205 packs (behind the TCA9546A
 * I2C mux) once per cycle and logs decoded telemetry. See fuel_gauge_driver.h.
 *
 * Created: October 2026
 * Authors: PVDX
 */

#include "logging.h"
#include "fuel_gauge_task.h"
#include "cmd_dispatcher.h"

// Fuel gauge task memory structures
fuel_gauge_task_memory_t fuel_gauge_mem;

/**
 * \fn main_fuel_gauge
 *
 * \param pvParameters a void pointer to the parameters required by the fuel gauge task;
 *      not currently set by config
 *
 * \warning should never return
 */
void main_fuel_gauge(void *pvParameters) {
    info("fuel_gauge: Task Started!\n");

    pvdx_task_t *const current_task = get_current_task();
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);

    status_t status = init_fuel_gauges();
    if (status != SUCCESS) {
        warning("fuel_gauge: init_fuel_gauges() failed (status=%d)\n", status);
    } else {
        info("fuel_gauge: initialized\n");
    }

    // Kept below the watchdog timeout/2 so the task still checks in on time.
    const TickType_t read_interval_ticks = pdMS_TO_TICKS(2000);

    while (true) {
        debug("\n---------- Fuel Gauge Task Loop ----------\n");

        fg_reading_t packs[FG_NUM_PACKS];
        status_t rs = fg_read_all(packs);
        if (rs != SUCCESS) {
            warning("fuel_gauge: fg_read_all failed (status=%d)\n", rs);
        } else {
            // Floats printed as integer milli-units to avoid relying on %f in printf.
            for (uint8_t ch = 0; ch < FG_NUM_PACKS; ch++) {
                fg_reading_t *p = &packs[ch];
                info("fuel_gauge: pack %u SOC=%ld m%%  Vpack=%ld mV  I=%ld mA  T=%ld mC\n", ch,
                     (long)(p->soc_pct * 1000.0f), (long)(p->pack_voltage * 1000.0f),
                     (long)(p->current_a * 1000.0f), (long)(p->temp_c * 1000.0f));
            }
        }

        if (fg_alrt_asserted()) {
            warning("fuel_gauge: ALRT asserted - scanning\n");
            fg_scan_alerts();
        }

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            while (enqueue_command(&cmd_checkin) != SUCCESS) {
                warning("fuel_gauge: Failed to enqueue watchdog checkin command\n");
            }
        }

        vTaskDelay(read_interval_ticks);
    }
}
