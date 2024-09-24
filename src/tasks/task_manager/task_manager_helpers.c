#include "task_manager_task.h"
#include "logging.h"

// Initializes the task manager task (it should be the first task in the global task list)
void task_manager_init(void) {
    task_manager_cmd_queue = xQueueCreateStatic(TASK_MANAGER_TASK_STACK_SIZE, TASK_MANAGER_QUEUE_ITEM_SIZE, task_manager_queue_buffer, &taskManagerMem.taskManagerTaskQueue);

    if (task_manager_cmd_queue == NULL) {
        fatal("task-manager: Failed to create task manager queue!\n");
    }

    if (taskList[0].function == &task_manager_main) {
        init_task(0);
    } else {
        fatal("Task Manager not found at index 0 of task list!\n");
    }
}

// Initialize all other tasks running on the system
void task_manager_init_subtasks(void) {
    for(int i = 0; taskList[i].name != NULL; i++) {
        // Verify that the current thread is the task 
        if (taskList[i].function == &task_manager_main) {
            // Sanity check: Make sure the task manager's handle is our current handle
            if (taskList[i].handle != xTaskGetCurrentTaskHandle()) {
                fatal("Task Manager handle does not match current task handle!\n");
            }

            continue;
        }
        
        init_task(i);
    }
}

// Initializes the task at index i in the task list
void init_task(int i) {
    if (!taskList[i].enabled) {
        info("task-manager: %s task is disabled. Skipped initialization.\n", taskList[i].name);
        return;
    }

    taskList[i].handle = xTaskCreateStatic(
        taskList[i].function, 
        taskList[i].name, 
        taskList[i].stackSize, 
        taskList[i].pvParameters, 
        taskList[i].priority, 
        taskList[i].stackBuffer, 
        taskList[i].taskTCB
    );

    if (taskList[i].handle == NULL) {
        fatal("task-manager: %s task creation failed!\n", taskList[i].name);
    } else {
        info("task-manager: %s task created!\n", taskList[i].name);
    }

    // Register the task with the watchdog (this will automatically handle duplicate initialization attempts)
    watchdog_register_task(taskList[i].handle);
}

status_t toggle_task(int i) {
    if (taskList[i].handle == NULL) {
        fatal("Task to be toggled was never initialized\n");
        return ERROR_UNINITIALIZED;
    }

    if (taskList[i].enabled) {
        vTaskSuspend(taskList[i].handle);
    } else {
        vTaskResume(taskList[i].handle);
    }
    taskList[i].enabled = !taskList[i].enabled;
    return SUCCESS;
}

status_t enable_task(int i) {
    // If given an unitialized task, inform and abort enabling
    if(taskList[i].handle == NULL) {
        info("task_manager: Attempted to enable uninitialized task");
        return ERROR_UNINITIALIZED;
    }
    // If given an already enabled task, inform then return success
    if(taskList[i].enabled) {
        info("task_manager: Given task had already been enabled");
        return SUCCESS;
    }
    vTaskResume(taskList[i].handle);
    taskList[i].enabled = true;
    
    return SUCCESS;
}

// Disables a task
status_t disable_task(int i) {
    if (taskList[i].handle == NULL) {
        fatal("task_manager: Trying to disale task that was never initialized");
        return ERROR_UNINITIALIZED;
    }
    // Only unregister the task from the watchdog if it has not already been uniregistered
    if (taskList[i].has_registered) {
        watchdog_unregister_task(taskList[i].handle);
        taskList[i].has_registered = false;
    }
    vTaskSuspend(taskList[i].handle);
    taskList[i].enabled = false;

    return SUCCESS;
}

// Returns the PVDXTask_t struct associated with a FreeRTOS task handle
PVDXTask_t* task_manager_get_task(TaskHandle_t handle) {
    for (int i = 0; taskList[i].name != NULL; i++) {
        if (taskList[i].handle == handle) {
            return &taskList[i];
        }
    }

    return &NULL_TASK;
}

void task_manager_exec(cmd_t cmd) {
    BaseType_t xStatus;

    switch (cmd.target) {
        case OPERATION_INIT_SUBTASKS:
            task_manager_init_subtasks();
            break;
        default:
            fatal("task-manager: Invalid operation!\n");
            break;
    }
}