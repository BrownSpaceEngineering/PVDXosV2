/**
 * task_manager_helpers.c
 * 
 * Helper functions for the task manager task. This task is responsible for initializing and enabling/disabling
 * all other tasks in the system based on PVDX's state diagram.
 * 
 * Created: April 14, 2024
 * Authors: Oren Kohavi, Ignacio Blancas Rodriguez, Tanish Makadia, Yi Liu, Siddharta Laloux, Aidan Wang, Simon Juknelis,
 * Defne Doken, Aidan Wang, Jai Garg, Alex Khosrowshahi
 */

#include "task_manager_task.h"

task_manager_task_memory_t task_manager_mem;
uint8_t task_manager_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
QueueHandle_t task_manager_command_queue_handle;
SemaphoreHandle_t task_list_mutex = NULL;
StaticSemaphore_t task_list_mutex_buffer;

void main_task_manager(void *pvParameters) {
    info("task-manager: Task Started!\n");

    // Enqueue a command to initialize all subtasks
    command_t command_task_manager_init_subtasks = {TASK_MANAGER, OPERATION_INIT_SUBTASKS, NULL, 0, NULL, NULL};
    enqueue_command(&command_task_manager_init_subtasks);
    debug("task_manager: Enqueued command to initialize all subtasks\n");

    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command();
    // Varible to hold commands popped off the queue
    command_t cmd;

    while (true) {
        debug_impl("\n---------- Task Manager Task Loop ----------\n");

        // Dispatch all commands contained in the queue
        while (xQueueReceive(task_manager_command_queue_handle, &cmd, 0) == pdPASS) {
            debug("task_manager: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
            exec_command_task_manager(cmd);
        }
        debug("task_manager: No more commands queued.\n");
        
        // Check in with the watchdog task
        enqueue_command(&cmd_checkin);
        debug("task_manager: Enqueued watchdog checkin command\n");
        // Wait 1 second before attempting to run the loop again
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}