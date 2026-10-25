#include "camera_task.h"
#include "cmd_dispatcher.h"
#include "logging.h"
#include "task_list.h"
#include "watchdog_task.h"

camera_task_memory_t camera_mem;

void main_camera(void *pvParameters) {
    (void)pvParameters;
    info("camera: Task started\n");

    pvdx_task_t *const current_task = get_current_task();
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    const TickType_t queue_block_time_ticks = pdMS_TO_TICKS(100);
    command_t cmd;

    while (true) {
        camera_park_if_stopped();
        // Process one command per iteration so a busy queue cannot starve watchdog check-ins.
        if (xQueueReceive(current_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            exec_command_camera(&cmd);
        }

        if (camera_stop_requested()) {
            continue;
        }
        if (should_checkin(current_task) && enqueue_command(&cmd_checkin) != SUCCESS) {
            warning("camera: Failed to enqueue watchdog checkin command\n");
        }
    }
}
