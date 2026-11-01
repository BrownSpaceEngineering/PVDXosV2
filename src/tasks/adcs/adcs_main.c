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
    QueueHandle_t adcs_command_queue_handle = xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE,
                                                                 adcs_mem.adcs_command_queue_buffer, &adcs_mem.adcs_task_queue);

    if (adcs_command_queue_handle == NULL) {
        fatal("Failed to create adcs command queue!\n");
    }

    // Initialize RTC timer hardware
    info("Initializing RTC timer\n");
    status_t result = init_rtc_hardware();
    if (result != SUCCESS) {
        warning("rtc timer: Hardware initialization failed\n");
    }

    // Leaves the coils off until the control loop drives them
    info("Initializing magnetorquers\n");
    result = init_magnetorquers();
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
    // than before the scheduler. The magnetometer has to go before the gyro, since they share an SPI bus and
    // init_rm3100() sets the bus's mode, which only works before init_gyro_hardware() enables the bus.
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

        // The coils are driven during this delay. vTaskDelayUntil is disabled in the ASF FreeRTOS config, so a whole
        // step takes this long plus its run time; dt is measured below so body() still sees the true step length
        vTaskDelay(pdMS_TO_TICKS(ADCS_LOOP_PERIOD_MS));

        // time since the last body() step, in seconds
        const TickType_t now_ticks = xTaskGetTickCount();
        const float dt = (float)(now_ticks - last_step_ticks) / configTICK_RATE_HZ;
        last_step_ticks = now_ticks;

        // Empty the queue without blocking: this loop is paced by the delay above, not by commands
        while (xQueueReceive(p_adcs_task->command_queue, &cmd, 0) == pdPASS) {
            debug("adcs: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
            exec_command_adcs(&cmd);
        }

        // The coils' own field would corrupt the magnetometer reading (and with it B-dot's dB/dt), so they are off
        // from before the measurement starts until its result has been used
        mtq_stop_all();
        vTaskDelay(pdMS_TO_TICKS(ADCS_MTQ_SETTLE_MS));

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

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            while (enqueue_command(&cmd_checkin) != SUCCESS) {
                warning("adcs: Failed to enqueue watchdog checkin command\n");
            }
            debug("adcs: Enqueued watchdog checkin command\n");
        }
    }
}
