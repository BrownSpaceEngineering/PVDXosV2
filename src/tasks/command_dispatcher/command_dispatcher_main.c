#include "command_dispatcher_task.h"

command_dispatcher_task_memory_t command_dispatcher_mem;
uint8_t command_dispatcher_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
QueueHandle_t command_dispatcher_command_queue_handle;

void main_command_dispatcher(void *pvParameters) {
    info("command_dispatcher: Task Started!\n");

    // Initialize the command queue
    init_command_dispatcher();

    command_t cmd;
    BaseType_t xStatus;
    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    command_t command_checkin = {TASK_WATCHDOG, OPERATION_CHECKIN, &handle, sizeof(TaskHandle_t*), NULL, NULL};

    while (true) {
        debug("command_dispatcher: Started main loop\n");
        // Check if there is a command to dispatch
        // ---------------------------------------
        // When xQueueReceive() is called with a non-zero timeout and the queue is empty, it will block the calling
        // task until either an item is received or the timeout period expires. If an item arrives during the timeout
        // period, the task will unblock immediately, retrieve the item, and proceed with processing. This way, the
        // command dispatcher task will not consume CPU cycles when there are no commands to execute.
        xStatus = xQueueReceive(command_dispatcher_command_queue_handle, &cmd, 0);

        if (xStatus == pdPASS) {
            // Command received, so dispatch it
            debug("command_dispatcher: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
            dispatch_command_command_dispatcher(cmd);
        } else {
            // No command received, so continue
            debug("command_dispatcher: No commands queued.\n");
        }

        // Check in with the watchdog
        command_dispatcher_enqueue(&command_checkin);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}