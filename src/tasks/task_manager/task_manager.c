#include "task_manager.h"

#include "logging.h"

struct taskManagerTaskMemory taskManagerMem;

/* {TaskHandle (NULL at initialization), main_func, "name", stack_size, argument, priority, stackbuffer, tcb}*/

PVDXTask_t taskList[] = {
    {NULL, watchdog_main, "Watchdog", WATCHDOG_TASK_STACK_SIZE, NULL, 2, watchdogMem.watchdogTaskStack, &watchdogMem.watchdogTaskTCB},
    {NULL, display_main, "DisplayMain", DISPLAY_TASK_STACK_SIZE, NULL, 1, displayMem.displayTaskStack, &displayMem.displayTaskTCB},
    {NULL, heartbeat_main, "Heartbeat", HEARTBEAT_TASK_STACK_SIZE, NULL, 1, heartbeatMem.heartbeatTaskStack, &heartbeatMem.heartbeatTaskTCB}
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL} // Null terminator for the array (since size is unspecified)
};

// Initializes all other tasks running on the system
void task_manager_init(void *pvParameters) {


    for(int i = 0; taskList[i].name != NULL; i++) {
        taskList[i].handle = xTaskCreateStatic(taskList[i].function, taskList[i].name, taskList[i].stackSize, taskList[i].pvParameters, taskList[i].priority, taskList[i].stackBuffer, taskList[i].taskTCB);
        if (taskList[i].handle == NULL) {
            fatal("task_manager_init: %s task creation failed!\n", taskList[i].name);
        } else {
            info("task_manager_init: %s task created!\n", taskList[i].name);
        }
        watchdog_register_task(taskList[i].handle);
    }

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

TaskHandle_t start_task() {

}