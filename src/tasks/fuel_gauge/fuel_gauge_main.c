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
#include "tasks/task_manager/state_machine.h"

// Fuel gauge task memory structures
fuel_gauge_task_memory_t fuel_gauge_mem;

// The state machine input this task owns (static: the task manager reads it after the command is sent)
static input_update_data_t fuel_gauge_inputs = {
    .input_update_bitmask = INPUT_UPDATE_BIT(BATTERY_LEVEL_UPDATE),
};

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
            warning("fuel_gauge: fg_read_all failed for at least one pack (status=%d)\n", rs);
        }
        // Floats printed as integer milli-units to avoid relying on %f in printf.
        // The battery level for the state machine is the lowest state of charge among the packs that read
        bool any_valid = false;
        float min_soc_pct = 0.0f;
        for (uint8_t ch = 0; ch < FG_NUM_PACKS; ch++) {
            fg_reading_t *p = &packs[ch];
            if (!p->valid) {
                warning("fuel_gauge: pack %u no data\n", ch);
                continue;
            }
            info("fuel_gauge: pack %u SOC=%ld m%%  Vpack=%ld mV  I=%ld mA  T=%ld mC\n", ch,
                 (long)(p->soc_pct * 1000.0f), (long)(p->pack_voltage * 1000.0f),
                 (long)(p->current_a * 1000.0f), (long)(p->temp_c * 1000.0f));
            if (!any_valid || p->soc_pct < min_soc_pct) {
                min_soc_pct = p->soc_pct;
            }
            any_valid = true;
        }

        // Report the battery level as a 0..1 fraction (the scale states.c uses). If no pack read, report full power
        // so that a dead fuel gauge never forces the satellite into low power.
        float level = 1.0f;
        if (any_valid) {
            level = min_soc_pct / 100.0f;
            level = (level < 0.0f) ? 0.0f : (level > 1.0f) ? 1.0f : level;
        } else {
            warning("fuel_gauge: no pack read, reporting full battery\n");
        }
        fuel_gauge_inputs.new_values.battery_level = level;
        command_t cmd_battery = get_state_input_update_command(&fuel_gauge_inputs);
        if (enqueue_command(&cmd_battery) != SUCCESS) {
            warning("fuel_gauge: Failed to enqueue battery level update\n");
        }
        info("fuel_gauge: battery level %ld/1000\n", (long)(level * 1000.0f));

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
