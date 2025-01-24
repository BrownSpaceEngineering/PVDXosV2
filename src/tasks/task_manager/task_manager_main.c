#include "task_manager_task.h"

task_manager_task_memory_t task_manager_mem;
uint8_t task_manager_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
QueueHandle_t task_manager_command_queue_handle;
SemaphoreHandle_t task_list_mutex = NULL;
StaticSemaphore_t task_list_mutex_buffer;

void main_task_manager(void *pvParameters) {
    info("task-manager: Started main loop\n");

    // Enqueue a command to initialize all subtasks
    status_t result;
    command_t command_task_manager_init_subtasks = {TASK_MANAGER, OPERATION_INIT_SUBTASKS, NULL, 0, &result, NULL};
    enqueue_command(&command_task_manager_init_subtasks);

    command_t cmd;
    BaseType_t xStatus;

    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    command_t command_checkin = {TASK_WATCHDOG, OPERATION_CHECKIN, &handle, sizeof(TaskHandle_t*), NULL, NULL};

    while (true) {
        debug("task_manager: Started main loop\n");
        // if there's something in the queue, pop it and execute it
        xStatus = xQueueReceive(task_manager_command_queue_handle, &cmd, 0);

        if (xStatus == pdPASS) {
            // Command received, so execute it
            debug("task_manager: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
            exec_command_task_manager(cmd);
        }
        else {
            // No command received, so continue
            debug("task_manager: No commands queued.\n");
        }
        
        enqueue_command(&command_checkin);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}