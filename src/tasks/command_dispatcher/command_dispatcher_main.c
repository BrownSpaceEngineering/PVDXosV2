/**
 * command_dispatcher_main.c
 * 
 * Main loop of the Command Dispatcher task. This task is responsible for receiving commands 
 * from other tasks and forwarding them to the appropriate task for execution. All major 
 * commands MUST be sent through the Command Dispatcher task to enable consistent logging and 
 * adhere to the PVDXos hub-and-spoke architecture.
 * 
 * Created: October 13, 2024
 * Authors: Tanish Makadia, Yi Liu
 */

#include "command_dispatcher_task.h"

command_dispatcher_task_memory_t command_dispatcher_mem;
uint8_t command_dispatcher_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
QueueHandle_t command_dispatcher_command_queue_handle;

void main_command_dispatcher(void *pvParameters) {
    info("command_dispatcher: Task Started!\n");

    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command();
    // Varible to hold commands popped off the queue
    command_t cmd;

    while (true) {
        debug_impl("\n---------- Command Dispatcher Task Loop ----------\n");

        // Dispatch all commands contained in the queue
        while (xQueueReceive(command_dispatcher_command_queue_handle, &cmd, 0) == pdPASS) {
            debug("command_dispatcher: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
            dispatch_command(cmd);
        }
        debug("command_dispatcher: No more commands queued.\n");

        // Check in with the watchdog task
        enqueue_command(&cmd_checkin);
        debug("command_dispatcher: Enqueued watchdog checkin command\n");
        
        // Wait 1 second before attempting to run the loop again
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}