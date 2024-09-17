#include "task_manager_task.h"

// typedef struct {
//     task_t target;
//     operation_t operation;
//     char* p_data;
//     size_t len;
//     status_t* p_result;
//     void (*callback)(status_t* p_result);
// } cmd_t;

void task_manager_main(void *pvParameters) {
    info("task-manager: Task started!\n");

    // Enqueue a command to initialize all subtasks
    status_t result;
    cmd_t command_task_manager_init_subtasks = {TASK_MANAGER, OPERATION_INIT_SUBTASKS, NULL, 0, &result, NULL};
    command_executor_enqueue(command_task_manager_init_subtasks);

    // Initialize all other tasks on the system
    // need to call command_executor_enqueue() to add the below function to the queue
    // task_manager_init_subtasks();
    // TODO: use the queue
    // TODO: create a function to enable a suspended task
    // TODO: create a function to disable a running task
    // (using the logic below)
    // Toggle task done!

    while (true) {
        // if there's something in the queue, pop it and execute it
        
        vTaskDelay(pdMS_TO_TICKS(1000));
        watchdog_checkin();
    }
}