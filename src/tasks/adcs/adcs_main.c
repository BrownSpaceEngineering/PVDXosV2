/**
 * adcs_main.c
 *
 * Main loop of the ADCS task, which reads the ADCS sensors, runs one adcs-c `body()` step and
 * drives the magnetorquers at a fixed rate
 *
 * Created: September 20, 2025
 * Modified: October 30, 2026
 * Authors: Avinash Patel, Yi Lyo, Alexander Thaep, Zach Mahan
 */

#include <string.h>

#include "checks/device_checks.h"
#include "drivers/magnetorquer/magnetorquer_driver.h"
#include "globals.h"
#include "libraries/adcs/include/body.h"
#include "libraries/adcs/include/photodiode_determination.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
#include "os_delay.h"
#include "state_machine.h"
#include "states.h"
#include "tasks/adcs/adcs_task.h"
#include "watchdog_task.h"

// ADCS Task memory structures
adcs_task_memory_t adcs_mem;

QueueHandle_t init_adcs(void) {
    QueueHandle_t adcs_command_queue_handle = xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE,
                                                                 adcs_mem.adcs_command_queue_buffer, &adcs_mem.adcs_task_queue);

    if (adcs_command_queue_handle == NULL) {
        fatal("Failed to create adcs command queue!\n");
    }

    // Leaves the coils off until the control loop drives them
    info("Initializing magnetorquers\n");
    status_t result = init_magnetorquers();
    if (result != SUCCESS) {
        warning("magnetorquers: Hardware initialization failed\n");
    }

    return adcs_command_queue_handle;
}

void main_adcs(void *pvParameters) {
    info("adcs: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Variable to hold commands popped off the queue
    command_t cmd;

    // Each device check also initializes that sensor. These wait for up to a few hundred ms, so they run here rather
    // than before the scheduler.
#ifdef ADCS_MOCK
    // The adcs-c plant simulates the sensors and magnetorquers, so the real ones are never touched, and the coils stay
    // off (init_adcs() leaves them off). Nothing is restored from MRAM, since the plant has its own orbit and clock.
    warning("adcs: ADCS_MOCK build, running against the simulated plant instead of the sensors and magnetorquers\n");
    body_reset();
    adcs_mock_init();
#else
    info("adcs: Initializing sensors\n");
    if (!check_device(MAGNETOMETER_ID)) {
        warning("adcs: magnetometer failed its device check\n");
    }
    if (!check_device(GYROSCOPE_ID)) {
        warning("adcs: gyro failed its device check\n");
    }
    if (!check_device(PHOTODIODE_ID)) {
        warning("adcs: photodiodes failed their device check\n");
    }

    // Start from the power-on filter / mode state, then bring back whatever orbit and time survived the last reboot
    body_reset();
    adcs_restore_state();
#endif

    // Sensor readings handed to body(); last_mag holds the previous step's reading for B-dot
    float last_mag[3];
    float mag[3];
    float gyro[3];
    float photodiodes[NUM_DIODES];
    float currents[3];

#ifndef ADCS_MOCK
    // seed last_mag so the first step sees no field change
    adcs_read_magnetometer(last_mag);

    TickType_t last_step_ticks = xTaskGetTickCount();
#endif

    while (true) {
        debug("\n---------- ADCS Task Loop ----------\n");

        // The coils are driven during this delay. vTaskDelayUntil is disabled in the ASF FreeRTOS config, so a whole
        // step takes this long plus its run time; dt is measured below so body() still sees the true step length
        os_delay_ms(ADCS_LOOP_PERIOD_MS);

        // The following example is provided for later because ADCS wants to average gyro 
        // readings over 100 ms, which is better done in the adcs task loop:

        // EXAMPLE (disabled): average the gyro over the coil-drive window instead of taking one sample per step. To use
        // it, replace the os_delay_ms() above with this, and after adcs_read_sensors() below copy gyro_avg into gyro.
        // The gyro is MEMS, so the coils being on doesn't disturb it. With the 13 Hz filter, samples closer than ~40 ms
        // are mostly correlated, so ~10 samples at 10 ms already gets nearly all of the noise reduction, and the 100 ms
        // boxcar (nulls at 10 Hz and multiples) stops noise above ~5 Hz aliasing into the ~9 Hz step rate. Samples that
        // fail or that the sensor flags come back as NAN from adcs_read_gyro() and are skipped; with none good, body()
        // gets NAN and bails as it does today.
        // NOTE: adcs_read_gyro() calls handle_read_failure() on every bad sample, and adcs_read_sensors() clears
        // sensor_fault at its start, so a fault seen only during this window is logged but doesn't stick.
        // NAN needs <math.h>, which this file doesn't include yet.
        //
        // #define ADCS_GYRO_AVG_SAMPLES 10
        // float gyro_avg[3] = {0.0f, 0.0f, 0.0f};
        // uint32_t gyro_good = 0;
        // for (uint32_t s = 0; s < ADCS_GYRO_AVG_SAMPLES; s++) {
        //     os_delay_ms(ADCS_LOOP_PERIOD_MS / ADCS_GYRO_AVG_SAMPLES);
        //     float sample[3];
        //     if (adcs_read_gyro(sample) == SUCCESS) {
        //         for (size_t i = 0; i < 3; i++) {
        //             gyro_avg[i] += sample[i];
        //         }
        //         gyro_good++;
        //     }
        // }
        // for (size_t i = 0; i < 3; i++) {
        //     gyro_avg[i] = (gyro_good > 0) ? gyro_avg[i] / (float)gyro_good : NAN;
        // }
        //
        // ...then after adcs_read_sensors(mag, gyro, photodiodes):
        //     memcpy(gyro, gyro_avg, sizeof(gyro));

        const TickType_t now_ticks = xTaskGetTickCount();
#ifdef ADCS_MOCK
        float dt; // the plant's fixed step, set by adcs_mock_sense() below
#else
        // time since the last body() step, in seconds
        const float dt = (float)(now_ticks - last_step_ticks) / configTICK_RATE_HZ;
        last_step_ticks = now_ticks;
#endif

        // Empty the queue without blocking: this loop is paced by the delay above, not by commands
        while (xQueueReceive(p_adcs_task->command_queue, &cmd, 0) == pdPASS) {
            debug("adcs: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
            exec_command_adcs(&cmd);
        }

#ifdef ADCS_MOCK
        // The plant gives its own last_mag, which carries its simulated sensor dropouts
        adcs_mock_sense(last_mag, mag, gyro, photodiodes, &dt);

        adcs_compute_currents(last_mag, mag, gyro, photodiodes, dt, currents);

        // As on hardware, the coils stay off in low power idle
        if (state_machine_get_current_state() == STATE_LOW_POWER_IDLE) {
            memset(currents, 0, sizeof(currents));
        }
        adcs_publish_telemetry();
        adcs_update_state_inputs();
        adcs_mock_step(currents, xTaskGetTickCount() - now_ticks);
#else
        // The coils' own field would corrupt the magnetometer reading (and with it B-dot's dB/dt), so they are off
        // from before the measurement starts until its result has been used
        mtq_stop_all();
        os_delay_ms(ADCS_MTQ_SETTLE_MS);

        // failed reads fill their buffers with values body() rejects, so it bails with zero currents
        adcs_read_sensors(mag, gyro, photodiodes);

        adcs_compute_currents(last_mag, mag, gyro, photodiodes, dt, currents);

        // The coils stay off in low power idle, since they were stopped above
        if (state_machine_get_current_state() != STATE_LOW_POWER_IDLE) {
            adcs_drive_magnetorquers(currents);
        }
        adcs_publish_telemetry();
        adcs_update_state_inputs();
        adcs_save_state();

        memcpy(last_mag, mag, sizeof(mag));
#endif

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            while (enqueue_command(&cmd_checkin) != SUCCESS) {
                warning("adcs: Failed to enqueue watchdog checkin command\n");
            }
            debug("adcs: Enqueued watchdog checkin command\n");
        }
    }
}
