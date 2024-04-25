#include "command_executor_task.h"

void command_executor_main(void *pvParameters) {
    // Initialize the command queue
    command_executor_init();

    info("command_executor: Task Started!");

    cmd_t* p_cmd;
    BaseType_t xStatus;

    while (true) {
        // TODO: Implement command execution
        // don't forget to checkin with the watchdog

        // Check if there is a command to execute
        xStatus = xQueueReceive(commandQueue, &p_cmd, 0);

        if (xStatus == pdPASS) {
            // Command received, so execute it
            command_executor_exec(p_cmd);
        }
        else {
            // No command to execute, or error occurred, delay
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}