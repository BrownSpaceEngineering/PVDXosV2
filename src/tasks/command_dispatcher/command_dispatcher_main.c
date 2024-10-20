#include "command_dispatcher_task.h"

command_dispatcher_task_memory_t command_dispatcher_mem;
uint8_t command_dispatcher_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
QueueHandle_t command_dispatcher_cmd_queue;

void main_command_dispatcher(void *pvParameters) {
    info("command_dispatcher: Task Started!");

    // Initialize the command queue
    init_command_dispatcher();

    command_t cmd;
    BaseType_t xStatus;

    while (true) {
        // Check if there is a command to dispatch
        // --------------------------------------
        // When xQueueReceive() is called with a non-zero timeout and the queue is empty, it will block the calling
        // task until either an item is received or the timeout period expires. If an item arrives during the timeout
        // period, the task will unblock immediately, retrieve the item, and proceed with processing. This way, the
        // command dispatcher task will not consume CPU cycles when there are no commands to execute.
        xStatus = xQueueReceive(command_dispatcher_cmd_queue, &cmd, pdMS_TO_TICKS(COMMAND_QUEUE_WAIT_MS));

        if (xStatus == pdPASS) {
            // Command received, so dispatch it
            debug("command_dispatcher: Command popped off queue.\n");
            exec_command_command_dispatcher(cmd);
        } else {
            // No command received, so continue
            debug("command_dispatcher: No commands queued.\n");
        }

        // Check in with the watchdog
        watchdog_checkin();
    }
}