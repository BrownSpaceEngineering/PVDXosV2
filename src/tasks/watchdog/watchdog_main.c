#include "watchdog_task.h"

struct watchdogTaskMemory watchdogMem;

void watchdog_main(void *pvParameters) {
    printf("watchdog: Task started!\n");

    while (1) {
        // Iterate through the running times and check if any tasks have not checked in within the allowed time
        uint32_t current_time = xTaskGetTickCount();

        for (int i = 0; i < NUM_TASKS; i++) {
            if (should_checkin[i]) {
                uint32_t time_since_last_checkin = current_time - running_times[i];

                if (time_since_last_checkin > allowed_times[i]) {
                    // The task has not checked in within the allowed time, so we should reset the system
                    printf("watchdog: Task %d has not checked in within the allowed time (time since last checkin: %d). Resetting system...\n", i, time_since_last_checkin);
                    watchdog_kick();
                }
            }
        }

        watchdog_checkin(WATCHDOG_TASK); // Watchdog checks in with itself
        
        // if we get here, then all tasks have checked in within the allowed time
        watchdog_pet();
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_MS_DELAY));
    }
}