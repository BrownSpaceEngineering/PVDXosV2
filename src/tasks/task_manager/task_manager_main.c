#include "task_manager_task.h"

task_manager_task_memory_t task_manager_mem;
uint8_t task_manager_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
QueueHandle_t task_manager_command_queue_handle;
SemaphoreHandle_t task_list_mutex = NULL;
StaticSemaphore_t task_list_mutex_buffer;


// TODO: tune watchdog timeout values
pvdx_task_t task_list[] = {
    // List of tasks to be initialized by the task manager (see pvdx_task_t definition in task_manager.h)
    // NOTE: Watchdog task must be first in the list, Command Dispatcher second, and Task Manager third
    // ***********************************************************************
    // ***  DO NOT CHANGE THE ORDER OF THE FIRST THREE OS-INTEGRITY TASKS  ***
    // ***********************************************************************
    // If you change the order of any of these, make sure that main.c reflects the change.
    {
        "Watchdog", true, NULL, main_watchdog, WATCHDOG_TASK_STACK_SIZE, watchdog_mem.watchdog_task_stack, NULL, 3, &watchdog_mem.watchdog_task_tcb, 10000, 0xDEADBEEF, false
    },
    {
        "CommandDispatcher", true, NULL, main_command_dispatcher, COMMAND_DISPATCHER_TASK_STACK_SIZE, command_dispatcher_mem.command_dispatcher_task_stack, NULL, 2, &command_dispatcher_mem.command_dispatcher_task_tcb, 10000, 0xDEADBEEF, false
    },
    {
        "TaskManager", true, NULL, main_task_manager, TASK_MANAGER_TASK_STACK_SIZE, task_manager_mem.task_manager_task_stack, NULL, 2, &task_manager_mem.task_manager_task_tcb, 10000, 0xDEADBEEF, false
    },
    // {
    //     "Shell", true, NULL, main_shell, SHELL_TASK_STACK_SIZE, shell_mem.shell_task_stack, NULL, 2, &shell_mem.shell_task_tcb, 10000, 0xDEADBEEF, false
    // },
    // {
    //     "Display", true, NULL, main_display, DISPLAY_TASK_STACK_SIZE, display_mem.display_task_stack, NULL, 2, &display_mem.display_task_tcb, 10000, 0xDEADBEEF, false
    // },
    // {
    //     "Heartbeat", true, NULL, main_heartbeat, HEARTBEAT_TASK_STACK_SIZE, heartbeat_mem.heartbeat_task_stack, NULL, 1, &heartbeat_mem.heartbeat_task_tcb, 10000, 0xDEADBEEF, false
    // },

    // Null terminator for the array (since size is unspecified)
    NULL_TASK
};

void main_task_manager(void *pvParameters) {
    info("task-manager: Started main loop\n");

    // Enqueue a command to initialize all subtasks
    status_t result;
    command_t command_task_manager_init_subtasks = {TASK_MANAGER, OPERATION_INIT_SUBTASKS, NULL, 0, &result, NULL};
    enqueue_command(&command_task_manager_init_subtasks);

    command_t cmd;
    BaseType_t xStatus;

    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    command_t command_checkin = {TASK_WATCHDOG, OPERATION_CHECKIN, &handle, sizeof(TaskHandle_t*), NULL, NULL};

    while (true) {
        debug("task_manager: Started main loop\n");
        // if there's something in the queue, pop it and execute it
        xStatus = xQueueReceive(task_manager_command_queue_handle, &cmd, 0);

        if (xStatus == pdPASS) {
            // Command received, so execute it
            debug("task_manager: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
            exec_command_task_manager(cmd);
        }
        else {
            // No command received, so continue
            debug("task_manager: No commands queued.\n");
        }
        
        enqueue_command(&command_checkin);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}