/**
 * Code for the RM3100 Magnetometer Sensor task
 *
 * Created: ?
 * Authors: Nathan Kim, Alexander Thaep, Siddharta Laloux
 **/

#include "magnetometer_task.h"

// Magnetometer Task memory structures
magnetometer_task_memory_t magnetometer_mem;

void main_magnetometer(void *pvParameters) {
    info("magnetometer: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    const command_t cmd_checkin = get_watchdog_checkin_command(current_task);
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
                exec_command_magnetometer(&cmd); // TODO
            } while (xQueueReceive(p_magnetometer_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("magnetometer: No more commands queued.\n");

        ;if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
        }
        debug("magnetometer: Enqueued watchdog checkin command\n");
    }
}

// OLD ONE (needs to be yoinked)

// void rm3100_main(void *pvParameters) {
//     if (init_rm3100() != SensorOK) {
//         return;
//     };

//     watchdog_checkin(RM3100_TASK);

//     mGain = 0.3671 * mCycleCount + 1.5;

//     while (1) {
//         while (gpio_get_pin_level(DRDY_PIN) == 0) {
//             vTaskDelay(pdMS_TO_TICKS(100));
//             watchdog_checkin(RM3100_TASK);
//         }

//         mag_read_data();
//     }
// }
