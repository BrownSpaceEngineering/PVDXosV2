/**
 * watchdog_main.c
 *
 * Main loop of the Watchdog RTOS task. This task is responsible for monitoring the check-ins of other tasks
 * and resetting the system if a task fails to check in within the allowed time.
 *
 * Created: January 28, 2024
 * Authors: Oren Kohavi, Tanish Makadia, Siddharta Laloux
 */

#include "watchdog_task.h"

watchdog_task_memory_t watchdog_mem;
QueueHandle_t watchdog_command_queue_handle;

/**
 * \fn main_watchdog
 *
 * \param pvParameters a void pointer to the parametres required by the 
 *      watchdog; not currently set by config
 *
 * \returns should never return
 */
void main_watchdog(void *pvParameters) {
    info("watchdog: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time the task should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;
    while (true) {
        debug_impl("\n---------- Watchdog Task Loop ----------\n");

        // Iterate through the running times and check if any tasks have not checked in within the allowed time
        const uint32_t current_time_ticks = xTaskGetTickCount();

        lock_mutex(task_list_mutex);

        for (size_t i = 0; task_list[i] != NULL; i++) {
            if (task_list[i]->has_registered) {
                const uint32_t ticks_since_last_checkin = current_time_ticks - task_list[i]->last_checkin_time_ticks;

                if (ticks_since_last_checkin > pdMS_TO_TICKS(task_list[i]->watchdog_timeout_ms)) {
                    // The task has not checked in within the allowed time, so we should reset the system
                    fatal(
                        "watchdog: %s task has not checked in within the allowed time! (time since last checkin: %d, allowed time: %d).\n",
                        task_list[i]->name, ticks_since_last_checkin, task_list[i]->watchdog_timeout_ms);
                }
            } else {
                debug("watchdog: Task %d has not registered, skipping it ...\n", i);
            }
        }

        unlock_mutex(task_list_mutex);

        // Execute all commands contained in the queue
        if (xQueueReceive(watchdog_command_queue_handle, &cmd, queue_block_time_ticks) == pdPASS) {
            do {
                debug("watchdog: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_watchdog(&cmd);
            } while (xQueueReceive(watchdog_command_queue_handle, &cmd, 0) == pdPASS);
        }
        debug("watchdog: No more commands queued.\n");

        // if we get here, then all tasks have checked in within the allowed time
        pet_watchdog();

        // Watchdog Task must also check-in with itself
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("watchdog: Enqueued watchdog checkin command\n");
        }
    }
}
