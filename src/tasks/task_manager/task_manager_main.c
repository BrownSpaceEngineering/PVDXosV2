#include "task_manager_task.h"

void task_manager_main(void *pvParameters) {
    info("task-manager: Task started!\n");

    // Initialize all other tasks on the system
    task_manager_init_subtasks();
    // TODO: create a function to enable a suspended task
    // TODO: create a function to disable a running task
    // (using the logic below)
    // Toggle task done!

    while (true) {
        // TODO: Add task manager logic here
        // don't forget to check in with the watchdog
        // for (uint32_t i = 0; taskList[i].name != NULL; i++) {
        //     // Prevent the task manager from disabling itself (xTaskGetCurrentTaskHandle() is the task manager's handle)
        //     if (taskList[i].handle != xTaskGetCurrentTaskHandle()) {
        //         if ((taskList[i].enabled)) {
        //             // There should never be a NULL handle if the task is enabled
        //             if (taskList[i].handle == NULL){
        //                 fatal("Task Supposed To Be Initialized, but it wasn't");
        //                 continue;
        //             }


        //            vTaskSuspend(taskList[i].handle);
        //         } else {
        //             // TODO: Figure out how to kill task
        //             if (taskList[i].handle == NULL) {
        //                 fatal("Task to be disabled was never initialized");
        //                 continue;
        //             }
                    
        //             vTaskResume(taskList[i].handle); 
        //         }
        //     }
        // }
        vTaskDelay(pdMS_TO_TICKS(1000));
        watchdog_checkin();
    }
}

    