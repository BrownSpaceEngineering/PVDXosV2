/**
 * adcs_main.c
 *
 * Main loop of the ADCS task, which reads the ADCS sensors, runs one adcs-c `body()` step and
 * drives the magnetorquers at a fixed rate
 *
 * Created: September 20, 2025
 * Modified: October 25, 2026
 * Authors: Avinash Patel, Yi Lyo, Alexander Thaep, Zach Mahan
 */

#include <string.h>

#include "drivers/gyro/gyro_driver.h"
#include "drivers/rtc/rtc_driver.h"
#include "globals.h"
#include "libraries/adcs/include/body.h"
#include "libraries/adcs/include/photodiode_determination.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
#include "state_machine.h"
#include "states.h"
#include "tasks/adcs/adcs_task.h"
#include "watchdog_task.h"

// ADCS Task memory structures
adcs_task_memory_t adcs_mem;

QueueHandle_t init_adcs(void) {
    // TODO: Uncomment once photodiode pin config is updated
    // status_t result = init_photodiode_hardware();
    // if (result != SUCCESS) {
    //     warning("photodiode: Hardware initialization failed\n");
    // }

    // TODO: Uncomment once magnetometer Atmel Start Config is restored
    // info("Initializing magnetometer\n");
    // result = init_rm3100();
    // if (result != SUCCESS) {
    //     warning("rm3100: Hardware initialization failed\n");
    // }

    // Initialize RTC timer hardware
    info("Initializing RTC timer\n");
    status_t result = init_rtc_hardware();
    if (result != SUCCESS) {
        warning("rtc timer: Hardware initialization failed\n");
    }

    // TODO: TLE and time-sync uplinks arrive on the command dispatcher's task, so ADCS will need a command queue
    // (returned here and set up in task_list.c) to hand them to this task safely.
    return NULL;
}

void main_adcs(void *pvParameters) {
    info("adcs: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);

    // The gyro startup sequence waits a few hundred ms, so it runs here rather than before the scheduler
    info("adcs: Initializing gyro\n");
    if (init_gyro_hardware() != SUCCESS) {
        warning("adcs: gyro hardware initialization failed\n");
    }

    // Start from the power-on filter / mode state
    body_reset();
    // TODO: restore the last saved TLE from MRAM here (needs a restore function in adcs-c that sets the saved
    // TLE and marks it initialized). Reject the record on a bad magic/CRC, invalid elements, or an epoch too far
    // from the current UTC (SGP4 error grows ~km/day), and wait for an uplink instead. UTC has to be restored
    // first, or the TLE is propagated to the wrong time. Attitude is not restored; the filter re-initializes
    // from detumbling.

    // Sensor readings handed to body(); last_mag holds the previous step's reading for B-dot
    float last_mag[3];
    float mag[3];
    float gyro[3];
    float photodiodes[NUM_DIODES];
    float currents[3];

    // seed last_mag so the first step sees no field change
    adcs_read_magnetometer(last_mag);

    TickType_t last_step_ticks = xTaskGetTickCount();

    while (true) {
        debug("\n---------- ADCS Task Loop ----------\n");

        // vTaskDelayUntil is disabled in the ASF FreeRTOS config, so the period drifts by the loop's run time;
        // dt is measured below so body() still sees the true step length
        vTaskDelay(pdMS_TO_TICKS(ADCS_LOOP_PERIOD_MS));

        // time since the last body() step, in seconds
        const TickType_t now_ticks = xTaskGetTickCount();
        const float dt = (float)(now_ticks - last_step_ticks) / configTICK_RATE_HZ;
        last_step_ticks = now_ticks;

        // failed reads fill their buffers with values body() rejects, so it bails with zero currents
        adcs_read_magnetometer(mag);
        adcs_read_gyro(gyro);
        adcs_read_photodiodes(photodiodes);

        int unix_time;
        int jd_scalar;
        float jd_frac;

        if (adcs_get_time(&unix_time, &jd_scalar, &jd_frac) == SUCCESS) {
            body(last_mag, mag, gyro, photodiodes, adcs_get_tle_update(), unix_time, jd_scalar, jd_frac, dt, currents);
            debug("adcs: did adcs body()\n");
        } else {
            // w/o a time there is no orbit or field model to compare against, so command nothing
            memset(currents, 0, sizeof(currents));
            // TODO actually, do bdot regardless here
            debug("adcs: skipped adcs body()\n");
        }

        if (state_machine_get_current_state() != STATE_LOW_POWER_IDLE) {
            adcs_drive_magnetorquers(currents);
        }
        adcs_publish_telemetry();

        // TODO: save the orbit to MRAM so it survives a reboot. Only when body() committed a new TLE this step
        // (it can still reject an uplinked one), not every loop: the 6 mean elements (float), B* (float) and the
        // epoch JD (double), plus a magic/version and CRC (or 3 majority-voted copies). Needs an adcs-c getter
        // for the committed TLE (e.g. body_get_tle()). Attitude and filter state are not saved; they are stale
        // within seconds. UTC must be saved too, but it belongs with the time sync, not here (see adcs_get_time()).

        memcpy(last_mag, mag, sizeof(mag));

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            while (enqueue_command(&cmd_checkin) != SUCCESS) {
                warning("adcs: Failed to enqueue watchdog checkin command\n");
            }
            debug("adcs: Enqueued watchdog checkin command\n");
        }
    }
}
