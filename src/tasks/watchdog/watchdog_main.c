#include "watchdog_task.h"

void watchdog_main(void *pvParameters) {
    // Iterate through the running times and check if any tasks have not checked in within the allowed time
    uint32_t current_time = xTaskGetTickCount();

    for (int i = 0; i < NUM_TASKS; i++) {
        if (should_checkin[i]) {
            uint32_t time_since_last_checkin = current_time - running_times[i];

            if (time_since_last_checkin > allowed_times[i]) {
                // The task has not checked in within the allowed time, so we should reset the system
                watchdog_kick();
            }
        }
    }

    // if we get here, then all tasks have checked in within the allowed time
    watchdog_pet();
    watchdog_checkin(WATCHDOG_TASK);
    vTaskDelay(pdMS_TO_TICKS(WATCHDOG_MS_DELAY));
}