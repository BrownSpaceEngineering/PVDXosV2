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
SemaphoreHandle_t task_list_mutex = NULL;
StaticSemaphore_t task_list_mutex_buffer;

/**
 * \fn main_task_manager
 *
 * \param pvParameters a void pointer to the parametres required by the task
 *      manager; not currently set by config
 *
 * \warning should never return
 */
void main_task_manager(void *pvParameters) {
    info("task-manager: Task Started!\n");

    // Enqueue a command to initialize all subtasks
    command_t command_task_manager_init_subtasks = {p_task_manager_task, OPERATION_INIT_SUBTASKS, NULL, 0, PROCESSING, NULL};
    enqueue_command(&command_task_manager_init_subtasks);
    debug("task_manager: Enqueued command to initialize all subtasks\n");
    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time the command dispatcher should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;

    while (true) {
        debug("\n---------- Task Manager Task Loop ----------\n");

        // Execute all commands contained in the queue
        if (xQueueReceive(p_task_manager_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            do {
                debug("task_manager: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_task_manager(&cmd);
            } while (xQueueReceive(p_task_manager_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("task_manager: No more commands queued.\n");

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("task_manager: Enqueued watchdog checkin command\n");
        }
    }
}
