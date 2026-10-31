#include "cmd_dispatcher.h"
#include "logging.h"
#include "perovskite_task.h"
#include "task_list.h"

perovskite_task_memory_t perovskite_mem;

void main_perovskite(void *pvParameters) {
    (void)pvParameters;
    info("perovskite: Task started\n");

    pvdx_task_t *const current_task = get_current_task();
    const TickType_t queue_block_time_ticks = pdMS_TO_TICKS(100);
    command_t cmd;

    while (true) {
        perovskite_park_if_stopped();
        if (xQueueReceive(current_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            exec_command_perovskite(&cmd);
        }

        if (perovskite_stop_requested()) {
            continue;
        }
        perovskite_checkin();
    }
}
