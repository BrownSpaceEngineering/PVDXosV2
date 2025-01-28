/**
 * watchdog_helpers.c
 * 
 * Helper functions for the watchdog task. This task is responsible for monitoring the check-ins of other tasks
 * and resetting the system if a task fails to check in within the allowed time.
 * 
 * Created: January 28, 2024
 * Authors: Oren Kohavi, Tanish Makadia
 */

#include "watchdog_task.h"

watchdog_task_memory_t watchdog_mem;
QueueHandle_t watchdog_command_queue_handle;
uint8_t watchdog_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
volatile Wdt *const p_watchdog = WDT;

void main_watchdog(void *pvParameters) {
    info("watchdog: Task Started!\n");

    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command();

    command_t cmd;
    BaseType_t xStatus;

    while (true) {
        debug("\n---------- Watchdog Task Loop ----------\n");

        // Iterate through the running times and check if any tasks have not checked in within the allowed time
        uint32_t current_time = xTaskGetTickCount();

        lock_mutex(task_list_mutex);

        for (size_t i = 0; task_list[i].name != NULL; i++) {
            if (task_list[i].has_registered) {
                uint32_t time_since_last_checkin = current_time - task_list[i].last_checkin;

                if (time_since_last_checkin > task_list[i].watchdog_timeout) {
                    // The task has not checked in within the allowed time, so we should reset the system
                    fatal(
                        "watchdog: %s task has not checked in within the allowed time! (time since last checkin: %d, allowed time: %d).\n",
                        task_list[i].name, time_since_last_checkin, task_list[i].watchdog_timeout
                    );
                }
            } else {
                debug("watchdog: Task %d has not registered, skipping it ...\n", i);
            }
        }

        unlock_mutex(task_list_mutex);

        // Pop all commands off of the watchdog command queue
        while ((xStatus = xQueueReceive(watchdog_command_queue_handle, &cmd, 0)) == pdPASS) {
            debug("watchdog: Command popped off queue.\n");
            exec_command_watchdog(cmd);
        }
        debug("watchdog: No more commands queued.\n");

        // if we get here, then all tasks have checked in within the allowed time
        pet_watchdog();
        // Watchdog checks in with itself
        enqueue_command(&cmd_checkin);
        // Wait for 1 second before monitoring task checkins again
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_MS_DELAY)); 
    }
}
