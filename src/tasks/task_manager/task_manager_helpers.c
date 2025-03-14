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

//
/**
 * \fn task_manager_init_subtasks
 *
 * \brief Initialise all subtasks (peripheral drivers, datastore, et al) tasks
 */
void task_manager_init_subtasks(void) {
    for (size_t i = 0; task_list[i] != NULL; i++) {
        if (task_list[i]->task_type != OS) {
            init_task_pointer(task_list[i]);
        }
    }
    debug("task_manager: All subtasks initialized\n");
}

/**
 * \fn task_manager_enable_task
 *
 * \brief Enables a task so that it can be run by the RTOS scheduler.
 *      Automatically registers the task with the watchdog.
 *
 * \param p_task constant task pointer corresponding to the task to be enabled
 *
 * \returns void
 *
 * \warning requires the task list mutex
 * \warning modifies a task struct
 */
void task_manager_enable_task(pvdx_task_t *const p_task) {
    lock_mutex(task_list_mutex);

    // If given an unintialized task, something went wrong
    if (p_task->handle == NULL) {
        fatal("task_manager: Attempted to enable uninitialized task");
    }

    // If given an already enabled task, something went wrong
    if (p_task->enabled) {
        fatal("task_manager: Tried to enable a task that has already been enabled");
    }

    vTaskResume(p_task->handle);
    p_task->enabled = true;

    // Register the task with the watchdog allowing it to be monitored
    register_task_with_watchdog(p_task);

    unlock_mutex(task_list_mutex);
    debug("task_manager: %s task enabled\n", p_task->name);
}

/**
 * \fn task_manager_disable_task
 *
 * \brief Disables a task so that it cannot be run by the RTOS scheduler. 
 *      Automatically unregisters the task with the watchdog.
 *
 * \param p_task constant task pointer corresponding to the task to be disabled
 *
 * \returns void
 *
 * \warning requires the task list mutex
 * \warning modifies a task struct
 */
void task_manager_disable_task(pvdx_task_t *const p_task) {
    lock_mutex(task_list_mutex);

    // If given an unintialized task, something went wrong
    if (p_task->handle == NULL) {
        fatal("task_manager: Trying to disable task that was never initialized");
    }

    // If given an already disabled task, something went wrong
    if (!p_task->enabled) {
        fatal("task_manager: Tried to disable a task that has already been disabled");
    }

    vTaskSuspend(p_task->handle);
    p_task->enabled = false;

    // Unregister the task with the watchdog so it is no longer monitored
    unregister_task_with_watchdog(p_task->handle);

    unlock_mutex(task_list_mutex);
    debug("task_manager: %s task disabled\n", p_task->name);
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

// Initializes the task manager task
/**
 * \fn init_task_manager
 *
 * \brief Initializes task manager task dependencies
 * 
 * Initialises task manager command queue, before `init_task_pointer()`. 
 * 
 * \returns QueueHandle_t, a handle to the created queue
 * 
 * \see `init_task_pointer()` for usage of functions of the type `init_<TASK>()`
 */
QueueHandle_t init_task_manager(void) {
    QueueHandle_t task_manager_command_queue_handle =
        xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, task_manager_mem.task_manager_command_queue_buffer,
                           &task_manager_mem.task_manager_task_queue);

    if (task_manager_command_queue_handle == NULL) {
        fatal("Failed to create task manager queue!\n");
    }

    return task_manager_command_queue_handle;
}

/**
 * \fn init_task_pointer
 *
 * \brief Initialises the task given by the pointer. Don't run if scheduler has started.
 *
 * \param p_task: a pointer to a PVDX task
 *
 * \return N/A
 *
 * \warning acquires the task list mutex
 * \warning modifies a task struct
 * 
 * \note calls `register_task_with_watchdog()`
 */
void init_task_pointer(pvdx_task_t *const p_task) {
    lock_mutex(task_list_mutex);

    // some functions don't have queues to initialise. init is NULL in such cases
    init_function task_init = p_task->init;
    if (task_init) {
        QueueHandle_t queue_handle = task_init();
        p_task->command_queue = queue_handle;
    }

    p_task->handle = xTaskCreateStatic(p_task->function, p_task->name, p_task->stack_size, p_task->pvParameters, p_task->priority,
                                       p_task->stack_buffer, p_task->task_tcb);

    // set thread-local array at index 0 to be a pointer to be a pointer
    // to the pvdx_task_t corresponding to the task.
    vTaskSetThreadLocalStoragePointer(p_task->handle, 0, (void *)p_task);

    if (p_task->handle == NULL) {
        fatal("failed to create %s task!\n", p_task->name);
    } else {
        debug("created %s task\n", p_task->name);
    }

    if (p_task->enabled) {
        // Register the task with the watchdog allowing it to be monitored
        register_task_with_watchdog(p_task);
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
 * \fn exec_command_task_manager 
 * 
 * \brief Executes function corresponding to the command
 * 
 * \param p_cmd a pointer to a command forwarded to the task manager
 * 
 * \return void
 * 
 * \warning fatal error if target wrong or operation undefined
 */
void exec_command_task_manager(command_t *const p_cmd) {
  
    if (p_cmd->target != p_task_manager_task) {
        fatal("task manager: command target is not task manager! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
    }

    switch (p_cmd->operation) {
        case OPERATION_INIT_SUBTASKS:
            task_manager_init_subtasks();
            p_cmd->result = SUCCESS;
            break;
        case OPERATION_ENABLE_SUBTASK:
            task_manager_enable_task((pvdx_task_t *)p_cmd->p_data); // Turn this into an index
            p_cmd->result = SUCCESS;
            break;
        case OPERATION_DISABLE_SUBTASK:
            task_manager_disable_task((pvdx_task_t *)p_cmd->p_data); // Turn this into an index
            p_cmd->result = SUCCESS;
            break;
        default:
            fatal("task-manager: Invalid operation!\n");
            p_cmd->result = ERROR_SANITY_CHECK_FAILED; // TODO: appropriate status?
            break;
    }
}
