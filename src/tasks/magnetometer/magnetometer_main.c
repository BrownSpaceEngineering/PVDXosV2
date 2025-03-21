/**
 * magnetometer_main.c
 * 
 * Main loop of the satellite's RM3100 Magnetometer sensor RTOS task
 *
 * Created: Feb 20, 2025
 * Authors: Nathan Kim, Alexander Thaep, Siddharta Laloux, Defne Doken, Aidan Wang, Tanish Makadia
 **/

#include "magnetometer_task.h"

// Magnetometer Task memory structures
magnetometer_task_memory_t magnetometer_mem;

/**
 * \fn main_magnetometer
 *
 * \param pvParameters a void pointer to the parametres required by the 
 *      magnetometer task; not currently set by config
 *
 * \warning should never return
 */
void main_magnetometer(void *pvParameters) {
    info("magnetometer: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time the command dispatcher should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;

    while (true) {
        debug_impl("\n---------- Magnetometer Task Loop ----------\n");

        // Execute all commands contained in the queue
        if (xQueueReceive(p_magnetometer_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            do {
                debug("magnetometer: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_magnetometer(&cmd);
            } while (xQueueReceive(p_magnetometer_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("magnetometer: No more commands queued.\n");

        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("magnetometer: Enqueued watchdog checkin command\n");
        }
    }
}