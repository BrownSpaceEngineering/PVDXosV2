#include "temperature_task.h"
#include "task_list.h"
#include "command_dispatcher_task.h"

temperature_task_memory_t temperature_mem;

void main_temperature(void *pvParameters) {
    (void)pvParameters;

    info("temperature: Task Started!\n");

    pvdx_task_t *const current_task = get_current_task();
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    command_t cmd;

    while (true) {
        if (current_task->command_queue &&
            xQueueReceive(current_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            do {
                exec_command_temperature(&cmd);
            } while (xQueueReceive(current_task->command_queue, &cmd, 0) == pdPASS);
        }

        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
        }
    }
}

