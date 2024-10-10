#include "watchdog_task.h"

watchdog_task_memory_t watchdog_mem;

volatile Wdt *const p_watchdog = WDT;
bool watchdog_enabled = false;

void watchdog_main(void *pvParameters) {
    info("watchdog: Task started!\n");

    while (1) {
        // Iterate through the running times and check if any tasks have not checked in within the allowed time
        uint32_t current_time = xTaskGetTickCount();

        for (size_t i = 0; task_list[i].name != NULL; i++) {
            if (task_list[i].enabled) {
                uint32_t time_since_last_checkin = current_time - task_list[i].last_checkin;

                if (time_since_last_checkin > task_list[i].watchdog_timeout) {
                    // The task has not checked in within the allowed time, so we should reset the system
                    fatal(
                        "watchdog: %s task has not checked in within the allowed time! (time since last checkin: %d, allowed time: %d). "
                        "Resetting system...\n",
                        task_list[i].name, time_since_last_checkin, task_list[i].watchdog_timeout);
                    watchdog_kick();
                }
            }
        }

        watchdog_checkin(); // Watchdog checks in with itself

        // if we get here, then all tasks have checked in within the allowed time
        watchdog_pet();
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_MS_DELAY));
    }
}