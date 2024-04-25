#include "task_manager_task.h"

void task_manager_main(void *pvParameters) {
    info("task-manager: Task started!\n");

    // Initialize all other tasks on the system
    task_manager_init_subtasks();

    while (true) {
        // TODO: Add task manager logic here
        // don't forget to check in with the watchdog
        vTaskDelay(pdMS_TO_TICKS(1000));
        watchdog_checkin();
    }
}

    