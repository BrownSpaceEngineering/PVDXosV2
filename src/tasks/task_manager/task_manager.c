#include "task_manager.h"

#include "logging.h"

struct taskManagerTaskMemory taskManagerMem;

// Initializes all other tasks running on the system
void task_manager_init(void *pvParameters) {
    // Create watchdog task
    // The watchdog task is responsible for checking in with all the other tasks and resetting the system if a task has
    // not checked in within the allowed time
    TaskHandle_t watchdogTaskHandle = xTaskCreateStatic(watchdog_main, "Watchdog", WATCHDOG_TASK_STACK_SIZE, NULL, 2,
                                                        watchdogMem.watchdogTaskStack, &watchdogMem.watchdogTaskTCB);

    watchdog_register_task(WATCHDOG_TASK); // Register the watchdog task with itself so that it can check in

    if (watchdogTaskHandle == NULL) {
        fatal("main: Watchdog task creation failed!\n");
    } else {
        info("main: Watchdog task created!\n");
    }

    // Create the display main task
    // The display task is responsible for updating the display using provided buffers
    TaskHandle_t displayTaskHandle = xTaskCreateStatic(display_main, "DisplayMain", DISPLAY_TASK_STACK_SIZE, NULL, 1,
                                                           displayMem.displayTaskStack, &displayMem.displayTaskTCB);

    watchdog_register_task(DISPLAY_TASK);

    if (displayTaskHandle == NULL) {
        fatal("main: Display task creation failed!\n");
    } else {
        info("main: Display task created!\n");
    }

    // Create the heartbeat task
    // The heartbeat task is a simple task that blinks the LEDs in a pattern to indicate that the system is running
    TaskHandle_t heartbeatTaskHandle = xTaskCreateStatic(heartbeat_main, "Heartbeat", HEARTBEAT_TASK_STACK_SIZE, NULL, 1,
                                                         heartbeatMem.heartbeatTaskStack, &heartbeatMem.heartbeatTaskTCB);

    watchdog_register_task(HEARTBEAT_TASK); // Register the heartbeat task with the watchdog so that it can check in

    if (heartbeatTaskHandle == NULL) {
        fatal("main: Heartbeat task creation failed!\n");
    } else {
        info("main: Heartbeat task created!\n");
    }

    // watchdog_checkin(TASK_MANAGER_TASK);
    // watchdog_unregister_task(TASK_MANAGER_TASK);

    // vTaskSuspend(NULL);
}