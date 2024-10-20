#include "task_manager_task.h"
#include "logging.h"

// General functions to interact with the global task list

// Initializes the task at index i in the task list
void init_task(size_t i) {
    // There may be tasks that are not enabled by default (on startup); if so, then they will have
    // task_list[i].enabled set to false. In this case, we should not initialize the task.
    if (!task_list[i].enabled) {
        info("task-manager: %s task is disabled. Skipped initialization.\n", task_list[i].name);
        return;
    }

    lock_mutex(task_list_mutex);
    
    task_list[i].handle = xTaskCreateStatic(
        task_list[i].function,
        task_list[i].name,
        task_list[i].stack_size,
        task_list[i].pvParameters,
        task_list[i].priority,
        task_list[i].stack_buffer,
        task_list[i].task_tcb
    );

    unlock_mutex(task_list_mutex);

    if (task_list[i].handle == NULL) {
        fatal("task-manager: %s task creation failed!\n", task_list[i].name);
    } else {
        info("task-manager: %s task created!\n", task_list[i].name);
    }

    // Register the task with the watchdog allowing it to be monitored
    register_task_with_watchdog(task_list[i].handle);
}

// Returns the pvdx_task_t struct associated with a FreeRTOS task handle
// WARNING: This function is not thread-safe and should only be called from within a critical section
pvdx_task_t* get_task(TaskHandle_t handle) {
    for (size_t i = 0; task_list[i].name != NULL; i++) {
        if (task_list[i].handle == handle) {
            return &task_list[i];
        }
    }

    return &NULL_TASK;
}

// Initializes the task manager task (it should be the first task in the global task list)
void init_task_manager(void) {
    task_manager_cmd_queue = xQueueCreateStatic(TASK_MANAGER_TASK_STACK_SIZE, COMMAND_QUEUE_ITEM_SIZE, task_manager_queue_buffer, &task_manager_mem.task_manager_task_queue);

    if (task_manager_cmd_queue == NULL) {
        fatal("task-manager: Failed to create task manager queue!\n");
    }

    if (task_list[0].function == &main_task_manager) {
        init_task(0);
    } else {
        fatal("Task Manager not found at index 0 of task list!\n");
    }
}

// Initialize all other tasks running on the system
void task_manager_init_subtasks(void) {
    for(size_t i = SUBTASK_START_INDEX; task_list[i].name != NULL; i++) {
        // Verify that the current thread is the task
        if (task_list[i].function == &main_task_manager) {
            // Sanity check: Make sure the task manager's handle is our current handle
            if (task_list[i].handle != xTaskGetCurrentTaskHandle()) {
                fatal("Task Manager handle does not match current task handle!\n");
            }

            continue;
        }

        init_task(i);
    }
}

status_t task_manager_enable_task(pvdx_task_t* task) {
    lock_mutex(task_list_mutex);
    status_t result;

    // If given an unintialized task, inform and abort enabling
    if (task->handle == NULL) {
        info("task_manager: Attempted to enable uninitialized task");
        result = ERROR_NULL_HANDLE;
    }

    // If given an already enabled task, inform then return success
    if (task->enabled) {
        info("task_manager: Tried to enable a task that has already been enabled");
        result = SUCCESS;
    }

    if (result != SUCCESS) {
        unlock_mutex(task_list_mutex);
        return result;
    }
    
    vTaskResume(task->handle);
    task->enabled = true;
    unlock_mutex(task_list_mutex);

    register_task_with_watchdog(task->handle);
    return result;
}

// Disables a task
status_t task_manager_disable_task(pvdx_task_t* task) {
    lock_mutex(task_list_mutex);
    status_t result;

    // If given an unintialized task, inform and abort disabling
    if (task->handle == NULL) {
        info("task_manager: Trying to disable task that was never initialized");
        result = ERROR_NULL_HANDLE;
    }

    // If given an already disabled task, inform then return success
    if (!task->enabled) {
        info("task_manager: Tried to disable a task that has already been disabled");
        result = SUCCESS;
    }

    if (result != SUCCESS) {
        unlock_mutex(task_list_mutex);
        return result;
    }

    vTaskSuspend(task->handle);
    task->enabled = false;
    unlock_mutex(task_list_mutex);

    unregister_task_with_watchdog(task->handle);
    return result;
}

void task_manager_exec(command_t cmd) {
    switch (cmd.operation) {
        case OPERATION_INIT_SUBTASKS:
            task_manager_init_subtasks();
            break;
        case OPERATION_ENABLE_SUBTASK:
            task_manager_enable_task(get_task((TaskHandle_t)cmd.p_data)); // Turn this into an index
            break;
        case OPERATION_DISABLE_SUBTASK:
            task_manager_disable_task(get_task((TaskHandle_t)cmd.p_data)); // Turn this into an index
            break;
        default:
            fatal("task-manager: Invalid operation!\n");
            break;
    }
}
