#include "task_manager_task.h"

struct taskManagerTaskMemory task_manager_mem;
uint8_t task_manager_queue_buffer[TASK_MANAGER_QUEUE_MAX_COMMANDS * TASK_MANAGER_QUEUE_ITEM_SIZE];
QueueHandle_t task_manager_cmd_queue;
SemaphoreHandle_t task_list_mutex = NULL;
StaticSemaphore_t task_list_mutex_buffer;


// TODO: tune watchdog timeout values
PVDXTask task_list[] = {
    // List of tasks to be initialized by the task manager (see PVDXTask definition in task_manager.h)
    // NOTE: Watchdog task must be first in the list, Command Executor second, and Task Manager third
    // DO NOT CHANGE THE ORDER OF THE FIRST THREE SUBTASKS !!!!
    {
        "Watchdog", true, NULL, watchdog_main, WATCHDOG_TASK_STACK_SIZE, watchdog_mem.watchdog_task_stack, NULL, 3, &watchdog_mem.watchdog_task_tcb, 1500, NULL, NULL
    },
    {
        "CommandExecutor", true, NULL, command_executor_main, COMMAND_EXECUTOR_TASK_STACK_SIZE, command_executor_mem.command_executor_task_stack, NULL, 2, &command_executor_mem.command_executor_task_tcb, 10000, NULL, NULL
    },
    {
        "TaskManager", true, NULL, task_manager_main, TASK_MANAGER_TASK_STACK_SIZE, task_manager_mem.task_manager_task_stack, NULL, 2, &task_manager_mem.task_manager_task_tcb, 5000, NULL, NULL
    },
    {
        "Shell", true, NULL, shell_main, SHELL_TASK_STACK_SIZE, shell_mem.shell_task_stack, NULL, 2, &shell_mem.shell_task_tcb, 10000, NULL, NULL
    },
    {
        "Display", true, NULL, display_main, DISPLAY_TASK_STACK_SIZE, display_mem.displayTaskStack, NULL, 2, &display_mem.display_task_tcb, 10000, NULL, NULL
    },
    {
        "Heartbeat", true, NULL, heartbeat_main, HEARTBEAT_TASK_STACK_SIZE, heartbeat_mem.heartbeat_task_stack, NULL, 1, &heartbeat_mem.heartbeat_task_tcb, 10000, NULL, NULL
    },

    // Null terminator for the array (since size is unspecified)
    NULL_TASK
};

void task_manager_main(void *pvParameters) {
    info("task-manager: Task started!\n");

    // Enqueue a command to initialize all subtasks
    Status result;
    Command command_task_manager_init_subtasks = {TASK_MANAGER, OPERATION_INIT_SUBTASKS, NULL, 0, &result, NULL};
    command_executor_enqueue(command_task_manager_init_subtasks);

    // Initialize a mutex wrapping the shared PVDX task list struct
    task_list_mutex = xSemaphoreCreateMutexStatic(&task_list_mutex_buffer);
    
    if (task_list_mutex == NULL){
        fatal("Failed to create PVDX task list mutex");
    }

    Command cmd;
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