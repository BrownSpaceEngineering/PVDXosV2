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

    task_list[i].handle = xTaskCreateStatic(
        task_list[i].function,
        task_list[i].name,
        task_list[i].stack_size,
        task_list[i].pvParameters,
        task_list[i].priority,
        task_list[i].stack_buffer,
        task_list[i].task_tcb
    );

    if (task_list[i].handle == NULL) {
        fatal("task-manager: %s task creation failed!\n", task_list[i].name);
    } else {
        info("task-manager: %s task created!\n", task_list[i].name);
    }

    // Register the task with the watchdog (this will automatically handle duplicate initialization attempts)
    watchdog_register_task(task_list[i].handle);
}

// Returns the pvdx_task_t struct associated with a FreeRTOS task handle
pvdx_task_t* get_task(TaskHandle_t handle) {
    for (size_t i = 0; task_list[i].name != NULL; i++) {
        if (task_list[i].handle == handle) {
            return &task_list[i];
        }
    }

    return &NULL_TASK;
}

// Initializes the task manager task (it should be the first task in the global task list)
void task_manager_init(void) {
    task_manager_cmd_queue = xQueueCreateStatic(TASK_MANAGER_TASK_STACK_SIZE, TASK_MANAGER_QUEUE_ITEM_SIZE, task_manager_queue_buffer, &task_manager_mem.taskManagerTaskQueue);

    if (task_manager_cmd_queue == NULL) {
        fatal("task-manager: Failed to create task manager queue!\n");
    }

    if (task_list[0].function == &task_manager_main) {
        init_task(0);
    } else {
        fatal("Task Manager not found at index 0 of task list!\n");
    }
}

// Initialize all other tasks running on the system
void task_manager_init_subtasks(void) {
    for(size_t i = SUBTASK_START_INDEX; task_list[i].name != NULL; i++) {
        // Verify that the current thread is the task
        if (task_list[i].function == &task_manager_main) {
            // Sanity check: Make sure the task manager's handle is our current handle
            if (task_list[i].handle != xTaskGetCurrentTaskHandle()) {
                fatal("Task Manager handle does not match current task handle!\n");
            }

            continue;
        }

        init_task(i);
    }
}

status_t task_manager_enable_task(size_t i) {
    lock_mutex(task_list_mutex);

    // If given an unitialized task, inform and abort enabling
    if(task_list[i].handle == NULL) {
        info("task_manager: Attempted to enable uninitialized task");
        return ERROR_UNINITIALIZED;
    }
    // If given an already enabled task, inform then return success
    if(task_list[i].enabled) {
        info("task_manager: Given task had already been enabled");
        return SUCCESS;
    }
    vTaskResume(task_list[i].handle);

    task_list[i].enabled = true;

    unlock_mutex(task_list_mutex);

    return SUCCESS;
}

// Disables a task
status_t task_manager_disable_task(size_t i) {
    lock_mutex(task_list_mutex);

    if (task_list[i].handle == NULL) {
        fatal("task_manager: Trying to disable task that was never initialized");
        return ERROR_UNINITIALIZED;
    }
    // Only unregister the task from the watchdog if it has not already been unregistered
    if (task_list[i].has_registered) {
        watchdog_unregister_task(task_list[i].handle);
        task_list[i].has_registered = false;
    }
    vTaskSuspend(task_list[i].handle);
    task_list[i].enabled = false;

    unlock_mutex(task_list_mutex);

    return SUCCESS;
}

void task_manager_exec(command_t cmd) {
    BaseType_t xStatus;

    switch (cmd.operation) {
        case OPERATION_INIT_SUBTASKS:
            task_manager_init_subtasks();
            break;
        case OPERATION_ENABLE_SUBTASK:
            enable_task(get_task(cmd.target)); // Turn this into an index
            break;
        default:
            fatal("task-manager: Invalid operation!\n");
            break;
    }
}
