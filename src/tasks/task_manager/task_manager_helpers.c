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

    status_t result;
    command_t register_task = {TASK_WATCHDOG, OPERATION_REGISTER_TASK, (char*)task_list[0].handle, sizeof(TaskHandle_t), &result, NULL};
    command_dispatcher_enqueue(register_task);
}

// Returns the pvdx_task_t struct associated with a FreeRTOS task handle
// WARNING: This function is not thread-safe and should only be called from within a critical section
pvdx_task_t* get_task(TaskHandle_t handle) {
    pvdx_task_t* p_task = task_list;

    while (p_task->name != NULL) {
        if (p_task->handle == handle) {
            return p_task;
        }
        p_task++;
    }
    
    return p_task;
}

// Initializes the task manager task (it should be the first task in the global task list)
void init_task_manager(void) {
    task_manager_command_queue = xQueueCreateStatic(TASK_MANAGER_TASK_STACK_SIZE, COMMAND_QUEUE_ITEM_SIZE, task_manager_queue_buffer, &task_manager_mem.task_manager_task_queue);

    if (task_manager_command_queue == NULL) {
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

void task_manager_enable_task(pvdx_task_t* task) {
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
    unlock_mutex(task_list_mutex);

    status_t result;
    command_t register_task = {TASK_WATCHDOG, OPERATION_REGISTER_TASK, (char*)task->handle, sizeof(TaskHandle_t), &result, NULL};
    command_dispatcher_enqueue(register_task);
}

// Disables a task
void task_manager_disable_task(pvdx_task_t* task) {
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
    unlock_mutex(task_list_mutex);

    status_t result;
    command_t unregister_task = {TASK_WATCHDOG, OPERATION_UNREGISTER_TASK, (char*)task->handle, sizeof(TaskHandle_t), &result, NULL};
    command_dispatcher_enqueue(unregister_task);
}

void exec_command_task_manager(command_t cmd) {
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
