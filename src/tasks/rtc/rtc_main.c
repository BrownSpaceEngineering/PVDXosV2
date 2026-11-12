/**
 * rtc_main.c
 *
 * Main loop of the RTC task, which calibrates the RTC against the UHF radio's timestamp counter once a second (see
 * rtc_driver.c). It is an OS task, so time keeps being calibrated in every mission state.
 *
 * Created: November 4, 2026
 * Authors: PVDX
 */

#include "tasks/rtc/rtc_task.h"

#include "cmd_dispatcher.h"
#include "drivers/rtc/rtc_driver.h"
#include "logging.h"

// RTC task memory structures
rtc_task_memory_t rtc_mem;

// Runs before the scheduler starts, so the RTC is counting before any task reads it
QueueHandle_t init_rtc(void) {
    info("rtc: Initializing RTC timer\n");
    if (init_rtc_hardware() != SUCCESS) {
        warning("rtc: Hardware initialization failed\n");
    }
    return NULL;
}

/**
 * \fn main_rtc
 *
 * \param pvParameters a void pointer to the parameters required by the RTC task; not currently set by config
 *
 * \warning should never return
 */
void main_rtc(void *pvParameters) {
    info("rtc: Task Started!\n");

    pvdx_task_t *const current_task = get_current_task();
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(RTC_TASK_PERIOD_MS));

        calibrate_rtc();

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            while (enqueue_command(&cmd_checkin) != SUCCESS) {
                warning("rtc: Failed to enqueue watchdog checkin command\n");
            }
            debug("rtc: Enqueued watchdog checkin command\n");
        }
    }
}
