#include "task_manager_task.h"

#include "logging.h"

struct taskManagerTaskMemory taskManagerMem;

PVDXTask_t taskList[] = {
    // List of tasks to be initialized by the task manager (see PVDXTask_t definition in task_manager.h)
    {NULL, watchdog_main, "Watchdog", WATCHDOG_TASK_STACK_SIZE, watchdogMem.watchdogTaskStack, NULL, 3, &watchdogMem.watchdogTaskTCB, 1500},
    {NULL, task_manager_main, "TaskManager", TASK_MANAGER_TASK_STACK_SIZE, taskManagerMem.taskManagerTaskStack, NULL, 2, &taskManagerMem.taskManagerTaskTCB, 5000},
    {NULL, display_main, "DisplayMain", DISPLAY_TASK_STACK_SIZE, displayMem.displayTaskStack, NULL, 2, &displayMem.displayTaskTCB, 10000},
    {NULL, heartbeat_main, "Heartbeat", HEARTBEAT_TASK_STACK_SIZE, heartbeatMem.heartbeatTaskStack, NULL, 1, &heartbeatMem.heartbeatTaskTCB, 10000},

    // Null terminator for the array (since size is unspecified)
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL} 
};

// Initialize all other tasks running on the system
void task_manager_init(void) {
    for(int i = 0; taskList[i].name != NULL; i++) {
        // Skip initialization of task manager since it has already been created in main.c
        if (taskList[i].name == "TaskManager") {
            continue;
        }

        taskList[i].handle = xTaskCreateStatic(
            taskList[i].function, 
            taskList[i].name, 
            taskList[i].stackSize, 
            taskList[i].pvParameters, 
            taskList[i].priority, 
            taskList[i].stackBuffer, 
            taskList[i].taskTCB
        );

        if (taskList[i].handle == NULL) {
            fatal("task_manager_init: %s task creation failed!\n", taskList[i].name);
        } else {
            info("task_manager_init: %s task created!\n", taskList[i].name);
        }

        watchdog_register_task(taskList[i].handle);
    }
}

TaskHandle_t start_task() {

}