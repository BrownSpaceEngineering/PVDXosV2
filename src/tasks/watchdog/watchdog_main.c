#include "watchdog_task.h"

watchdog_task_memory_t watchdog_mem;
QueueHandle_t watchdog_command_queue_handle;
uint8_t watchdog_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];

volatile Wdt *const p_watchdog = WDT;
bool watchdog_enabled = false;

void main_watchdog(void *pvParameters) {
    info("watchdog: Task started!\n");

    command_t cmd;
    BaseType_t xStatus;

    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    command_t command_checkin = {TASK_WATCHDOG, OPERATION_CHECKIN, &handle, sizeof(TaskHandle_t*), NULL, NULL};

    while (1) {
        debug("watchdog: Started main loop\n");
        // Iterate through the running times and check if any tasks have not checked in within the allowed time
        uint32_t current_time = xTaskGetTickCount();

        lock_mutex(task_list_mutex);

        for (size_t i = 0; task_list[i].name != NULL; i++) {
            debug("watchdog: Determining whether task %d has checked in\n", i);
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
        debug("watchdog: No commands queued.\n");

        // if we get here, then all tasks have checked in within the allowed time
        pet_watchdog();
        
        // Wait for 1 second before monitoring task checkins again
        enqueue_command(&command_checkin); // Watchdog checks in with itself
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_MS_DELAY)); 
    }
}
