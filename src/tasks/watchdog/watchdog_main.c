#include "watchdog_task.h"

watchdog_task_memory_t watchdog_mem;

volatile Wdt *const p_watchdog = WDT;
bool watchdog_enabled = false;

void main_watchdog(void *pvParameters) {
    info("watchdog: Task started!\n");

    command_t cmd;
    BaseType_t xStatus;

    while (1) {
        // Iterate through the running times and check if any tasks have not checked in within the allowed time
        uint32_t current_time = xTaskGetTickCount();

        // Acquire the mutex to prevent the task list from being modified while we are iterating through it
        lock_mutex(task_list_mutex);

        for (size_t i = 0; task_list[i].name != NULL; i++) {
            if (task_list[i].has_registered) {
                uint32_t time_since_last_checkin = current_time - task_list[i].last_checkin;

                if (time_since_last_checkin > task_list[i].watchdog_timeout) {
                    // The task has not checked in within the allowed time, so we should reset the system
                    fatal(
                        "watchdog: %s task has not checked in within the allowed time! (time since last checkin: %d, allowed time: %d). "
                        "Resetting system...\n",
                        task_list[i].name, time_since_last_checkin, task_list[i].watchdog_timeout);
                    kick_watchdog();
                }
            }
        }

        // Pop any commands off of the watchdog command queue
        xStatus = xQueueReceive(watchdog_command_queue, &cmd, pdMS_TO_TICKS(COMMAND_QUEUE_WAIT_MS));

        if (xStatus == pdPASS) {
            // Command received, so execute it
            debug("watchdog: Command popped off queue.\n");
            exec_command_watchdog(cmd);
        } else {
            // No command received, so continue
            debug("task_manager: No commands queued.\n");
        }

        // Release the mutex
        unlock_mutex(task_list_mutex);

        watchdog_checkin(); // Watchdog checks in with itself

        // if we get here, then all tasks have checked in within the allowed time
        pet_watchdog();
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_MS_DELAY));
    }
}