/**
 * task_manager_helpers.c
 *
 * Helper functions for the task manager task. This task is responsible for initializing and enabling/disabling
 * all other tasks in the system based on PVDX's state diagram.
 *
 * Created: April 14, 2024
 * Authors: Oren Kohavi, Ignacio Blancas Rodriguez, Tanish Makadia, Yi Liu, Aidan Wang, Simon Juknelis, Defne Doken,
 * Aidan Wang, Jai Garg, Alex Khosrowshahi, Siddharta Laloux
 */

#include "logging.h"
#include "task_manager_task.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

// Initialize all peripheral device driver tasks running on PVDXos
void task_manager_init_subtasks(void) {
    for (size_t i = SUBTASK_START_INDEX; task_list[i] != NULL; i++) {
        init_task_index(i);
    }
    debug("task_manager: All subtasks initialized\n");
}

// Enables a task so that it can be run by the RTOS scheduler. Automatically registers the task with the watchdog.
void task_manager_enable_task(pvdx_task_t *const task) {
    lock_mutex(task_list_mutex);

    // If given an unintialized task, something went wrong
    if (task->handle == NULL) {
        fatal("task_manager: Attempted to enable uninitialized task");
    }

    // If given an already enabled task, something went wrong
    if (task->enabled) {
        fatal("task_manager: Tried to enable a task that has already been enabled");
    }

    vTaskResume(task->handle);
    task->enabled = true;

    // Register the task with the watchdog allowing it to be monitored
    register_task_with_watchdog(task->handle);

    unlock_mutex(task_list_mutex);
    debug("task_manager: %s task enabled\n", task->name);
}

// Disables a task so that it can not be run by the RTOS scheduler. Automatically unregisters the task with the watchdog.
void task_manager_disable_task(pvdx_task_t *const task) {
    lock_mutex(task_list_mutex);

    // If given an unintialized task, something went wrong
    if (task->handle == NULL) {
        fatal("task_manager: Trying to disable task that was never initialized");
    }

    // If given an already disabled task, something went wrong
    if (!task->enabled) {
        fatal("task_manager: Tried to disable a task that has already been disabled");
    }

    vTaskSuspend(task->handle);
    task->enabled = false;

    // Unregister the task with the watchdog so it is no longer monitored
    unregister_task_with_watchdog(task->handle);

    unlock_mutex(task_list_mutex);
    debug("task_manager: %s task disabled\n", task->name);
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

// Initializes the task manager task
void init_task_manager(void) {
    task_manager_command_queue_handle = xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE,
                                                           task_manager_command_queue_buffer, &task_manager_mem.task_manager_task_queue);

    if (task_manager_command_queue_handle == NULL) {
        fatal("Failed to create task manager queue!\n");
    }
}

// Initializes the task at index i in the task list
void init_task_index(const size_t i) {
    lock_mutex(task_list_mutex);

    task_list[i]->handle =
        xTaskCreateStatic(task_list[i]->function, task_list[i]->name, task_list[i]->stack_size, task_list[i]->pvParameters,
                          task_list[i]->priority, task_list[i]->stack_buffer, task_list[i]->task_tcb);

    if (task_list[i]->handle == NULL) {
        fatal("failed to create %s task!\n", task_list[i]->name);
    } else {
        debug("created %s task\n", task_list[i]->name);
    }

    if (task_list[i]->enabled) {
        // Register the task with the watchdog allowing it to be monitored
        register_task_with_watchdog(task_list[i]->handle);
    } else {
        // There may be tasks that are disabled on startup; if so, then they MUST have task_list[i].enabled
        // set to false. In this case, we still allocate memory and create the task, but immediately suspend
        // it so that vTaskStartScheduler() doesn't run the task.
        vTaskSuspend(task_list[i]->handle);
        info("%s task is disabled on startup.\n", task_list[i]->name);
    }

    unlock_mutex(task_list_mutex);
}

/**
 * init_task_pointer(pvdx_task_t *const p_task)
 *
 * Initialises the task given by the pointer.
 *
 * Parametre:
 *      p_task: a pointer to a pvdx_task_t
 *
 * Returns: N/A
 *
 * Warning: Modifies the task list
 */
void init_task_pointer(pvdx_task_t *const p_task) {
    lock_mutex(task_list_mutex);

    p_task->command_queue = xQueueCreateStatic();

    p_task->handle = xTaskCreateStatic(p_task->function, p_task->name, p_task->stack_size, p_task->pvParameters, p_task->priority,
                                       p_task->stack_buffer, p_task->task_tcb);

    if (p_task->handle == NULL) {
        fatal("failed to create %s task!\n", p_task->name);
    } else {
        debug("created %s task\n", p_task->name);
    }

    if (p_task->enabled) {
        // Register the task with the watchdog allowing it to be monitored
        register_task_with_watchdog(p_task->handle);
    } else {
        // There may be tasks that are disabled on startup; if so, then they MUST have task_list[i].enabled
        // set to false. In this case, we still allocate memory and create the task, but immediately suspend
        // it so that vTaskStartScheduler() doesn't run the task.
        vTaskSuspend(p_task->handle);
        info("%s task is disabled on startup.\n", p_task->name);
    }

    unlock_mutex(task_list_mutex);
}

/**
 * init_task_pointer(pvdx_task_t *const handler)
 *
 * Initialises the task given by the task
 *
 * Parametre:
 *      p_task: a pointer to a pvdx_task_t
 *
 * Returns: N/A
 *
 * Warning: Modifies the task list
 */
void init_task_handle(TaskHandle_t handle) {
    pvdx_task_t *p_task = get_task(handle);

    init_task_pointer(p_task);
}

void exec_command_task_manager(command_t *const p_cmd) {
    if (p_cmd->target != TASK_MANAGER) {
        fatal("task manager: command target is not task manager! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
    }

    switch (p_cmd->operation) {
        case OPERATION_INIT_SUBTASKS:
            task_manager_init_subtasks();
            break;
        case OPERATION_ENABLE_SUBTASK:
            task_manager_enable_task(get_task(*(TaskHandle_t *)p_cmd->p_data)); // Turn this into an index
            break;
        case OPERATION_DISABLE_SUBTASK:
            task_manager_disable_task(get_task(*(TaskHandle_t *)p_cmd->p_data)); // Turn this into an index
            break;
        default:
            fatal("task-manager: Invalid operation!\n");
            break;
    }
}
