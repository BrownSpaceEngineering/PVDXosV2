#include "task_manager_task.h"

struct taskManagerTaskMemory taskManagerMem;
uint8_t task_manager_queue_buffer[TASK_MANAGER_QUEUE_MAX_COMMANDS * TASK_MANAGER_QUEUE_ITEM_SIZE];
QueueHandle_t task_manager_cmd_queue;
SemaphoreHandle_t task_list_mutex = NULL;
StaticSemaphore_t task_list_mutex_buffer;


// TODO: tune watchdog timeout values
PVDXTask_t taskList[] = {
    // List of tasks to be initialized by the task manager (see PVDXTask_t definition in task_manager.h)
    // NOTE: Task Manager task must be the first task in the list
    {
        "TaskManager", true, NULL, task_manager_main, TASK_MANAGER_TASK_STACK_SIZE, taskManagerMem.taskManagerTaskStack, NULL, 2, &taskManagerMem.taskManagerTaskTCB, 5000, NULL, NULL
    },
    {
        "Watchdog", true, NULL, watchdog_main, WATCHDOG_TASK_STACK_SIZE, watchdogMem.watchdogTaskStack, NULL, 3, &watchdogMem.watchdogTaskTCB, 1500, NULL, NULL
    },
    {
        "CommandExecutor", true, NULL, command_executor_main, COMMAND_EXECUTOR_TASK_STACK_SIZE, commandExecutorMem.commandExecutorTaskStack, NULL, 2, &commandExecutorMem.commandExecutorTaskTCB, 10000, NULL, NULL
    },
    {
        "Shell", true, NULL, shell_main, SHELL_TASK_STACK_SIZE, shellMem.shellTaskStack, NULL, 2, &shellMem.shellTaskTCB, 10000, NULL, NULL
    },
    {
        "Display", true, NULL, display_main, DISPLAY_TASK_STACK_SIZE, displayMem.displayTaskStack, NULL, 2, &displayMem.displayTaskTCB, 10000, NULL, NULL
    },
    {
        "Heartbeat", true, NULL, heartbeat_main, HEARTBEAT_TASK_STACK_SIZE, heartbeatMem.heartbeatTaskStack, NULL, 1, &heartbeatMem.heartbeatTaskTCB, 10000, NULL, NULL
    },

    // Null terminator for the array (since size is unspecified)
    NULL_TASK
};

void task_manager_main(void *pvParameters) {
    info("task-manager: Task started!\n");

    // Enqueue a command to initialize all subtasks
    status_t result;
    cmd_t command_task_manager_init_subtasks = {TASK_MANAGER, OPERATION_INIT_SUBTASKS, NULL, 0, &result, NULL};
    command_executor_enqueue(command_task_manager_init_subtasks);

    // Initialize a mutex wrapping the shared PVDX task list struct
    task_list_mutex = xSemaphoreCreateMutexStatic(&task_list_mutex_buffer);
    
    if (task_list_mutex == NULL){
        fatal("Failed to create PVDX task list mutex");
    }

    cmd_t cmd;
    BaseType_t xStatus;

    while (true) {
        // if there's something in the queue, pop it and execute it
        xStatus = xQueueReceive(task_manager_cmd_queue, &cmd, pdMS_TO_TICKS(COMMAND_QUEUE_WAIT_MS));

        if (xStatus == pdPASS) {
            // Command received, so execute it
            debug("task_manager: Command popped off queue.\n");
            task_manager_exec(cmd);
        }
        else {
            // No command received, so continue
            debug("task_manager: No commands queued.\n");
        }
        
        // vTaskDelay(pdMS_TO_TICKS(1000));
        watchdog_checkin();
    }
}