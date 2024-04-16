#include "task_manager_task.h"

#include "logging.h"

struct taskManagerTaskMemory taskManagerMem;

#define NULL_TASK ((PVDXTask_t){NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL})

PVDXTask_t taskList[] = {
    // List of tasks to be initialized by the task manager (see PVDXTask_t definition in task_manager.h)
    {
        "Watchdog", WATCHDOG_TASK_ID, NULL, watchdog_main, WATCHDOG_TASK_STACK_SIZE, watchdogMem.watchdogTaskStack, NULL, 3, &watchdogMem.watchdogTaskTCB, 1500, NULL, NULL
    },
    {
        "TaskManager", TASK_MANAGER_TASK_ID, NULL, task_manager_main, TASK_MANAGER_TASK_STACK_SIZE, taskManagerMem.taskManagerTaskStack, NULL, 2, &taskManagerMem.taskManagerTaskTCB, 5000, NULL, NULL
    },
    {
        "CommandExecutor", COMMAND_EXECUTOR_TASK_ID, NULL, command_executor_main, COMMAND_EXECUTOR_TASK_STACK_SIZE, commandExecutorMem.commandExecutorTaskStack, NULL, 2, &commandExecutorMem.commandExecutorTaskTCB, 10000, NULL, NULL
    },
    {
        "Display", DISPLAY_TASK_ID, NULL, display_main, DISPLAY_TASK_STACK_SIZE, displayMem.displayTaskStack, NULL, 2, &displayMem.displayTaskTCB, 10000, NULL, NULL
    },
    {
        "Heartbeat", HEARTBEAT_TASK_ID, NULL, heartbeat_main, HEARTBEAT_TASK_STACK_SIZE, heartbeatMem.heartbeatTaskStack, NULL, 1, &heartbeatMem.heartbeatTaskTCB, 10000, NULL, NULL
    },

    // Null terminator for the array (since size is unspecified)
    NULL_TASK
};

// Initialize all other tasks running on the system
void task_manager_init(void) {
    for(int i = 0; taskList[i].id != NULL; i++) {
        // Skip initialization of task manager since it has already been created in main.c
        if (taskList[i].id == TASK_MANAGER_TASK_ID) {
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

        watchdog_register_task(taskList[i].id);
    }
}

// Get a task from the global task list by its ID
PVDXTask_t task_manager_get_task(TaskID_t id) {
    for (int i = 0; taskList[i].id != NULL; i++) {
        if (taskList[i].id == id) {
            return taskList[i];
        }
    }

    return NULL_TASK;
}