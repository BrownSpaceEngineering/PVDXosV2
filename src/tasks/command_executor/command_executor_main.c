#include "command_executor_task.h"

void command_executor_main(void *pvParameters) {
    // Initialize the command queue
    command_executor_init();

    info("command_executor: Task Started!");

    cmd_t* p_cmd;
    BaseType_t xStatus;

    while (true) {
        // Check if there is a command to execute
        // --------------------------------------
        // When xQueueReceive() is called with a non-zero timeout and the queue is empty, it will block the calling 
        // task until either an item is received or the timeout period expires. If an item arrives during the timeout 
        // period, the task will unblock immediately, retrieve the item, and proceed with processing. This way, the
        // command executor task will not consume CPU cycles when there are no commands to execute.
        xStatus = xQueueReceive(commandQueue, &p_cmd, pdMS_TO_TICKS(COMMAND_QUEUE_WAIT_MS));

        if (xStatus == pdPASS) {
            // Command received, so execute it
            command_executor_exec(p_cmd);
        }

        // Check in with the watchdog
        watchdog_checkin();
    }
}