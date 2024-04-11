#include "task_manager.h"

#include "logging.h"

struct taskManagerTaskMemory taskManagerMem;

// Initializes the sensors
void task_manager_init(void *pvParameters) {
    debug("Task Manager gone through\n");
    // Initialize the display
    display_init();
    info("Display initialized\n");
    watchdog_checkin(TASK_MANAGER_TASK);
    watchdog_unregister_task(TASK_MANAGER_TASK);

    vTaskSuspend(NULL);
}