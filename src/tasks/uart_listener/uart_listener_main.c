/**
 * uart_listener_main.c
 *
 * Created: November 13, 2025
 * Authors:
 */

#include "uart_listener_task.h"

uart_listener_task_memory_t uart_listener_mem;

/**
 * \fn main_uart_listener
 *
 * \param pvParameters a void pointer to the parametres required by uart_listener; not currently set by config
 *
 * \warning should never return
 */
void main_uart_listener(void *pvParameters) {
    info("uart_listener: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time this task should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;

    while (true) {
        debug_impl("\n---------- uart_listener Task Loop ----------\n");

        // Block waiting for at least one command to appear in the command queue
        if (xQueueReceive(p_uart_listener_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            // Once there is at least one command in the queue, empty the entire queue
            do {
                debug("uart_listener: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_uart_listener(&cmd); 
                // TODO: implement contents of main loop. 

            } while (xQueueReceive(p_uart_listener_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("uart_listener: No more commands queued.\n");

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
        }
        debug("uart_listener: Enqueued watchdog checkin command\n");
    }
}
