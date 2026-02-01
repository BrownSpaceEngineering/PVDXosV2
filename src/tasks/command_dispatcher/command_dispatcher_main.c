/**
 * command_dispatcher_main.c
 *
 * Main loop of the Command Dispatcher task. This task is responsible for receiving commands
 * from other tasks and forwarding them to the appropriate task for execution. All major
 * commands MUST be sent through the Command Dispatcher task to enable consistent logging and
 * adhere to the PVDXos hub-and-spoke architecture.
 *
 * Created: October 13, 2024
 * Authors: Tanish Makadia, Yi Liu, Siddharta Laloux
 */

#include "command_dispatcher_task.h"

command_dispatcher_task_memory_t command_dispatcher_mem;

/**
 * \fn main_command_dispatcher
 *
 * \param pvParameters a void pointer to the parametres required by the command
 *      dispatcher; not currently set by config
 *
 * \warning should never return
 */
void main_command_dispatcher(void *pvParameters) {
    info("command_dispatcher: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time the command dispatcher should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;

    while (true) {
        debug("\n---------- Command Dispatcher Task Loop ----------\n");

        // Block waiting for at least one command to appear in the command queue
        if (xQueueReceive(p_command_dispatcher_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            // Once there is at least one command in the queue, empty the entire queue
            do {
                debug("command_dispatcher: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                dispatch_command(&cmd);
            } while (xQueueReceive(p_command_dispatcher_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("command_dispatcher: No more commands queued.\n");

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("command_dispatcher: Enqueued watchdog checkin command\n");
        }

        // Guarantee other tasks get time to run
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
