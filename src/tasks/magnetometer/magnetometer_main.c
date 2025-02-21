#include "magnetometer_task.h"

// Magnetometer Task memory structures
magnetometer_task_memory_t magnetometer_mem;
uint8_t magnetometer_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
QueueHandle_t magnetometer_command_queue_handle;

void main_magnetometer(void *pvParameters) {
    info("magnetometer: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_task(xTaskGetCurrentTaskHandle());
    // Cache the watchdog checkin command to avoid creating it every iteration
    const command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time the command dispatcher should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;

    // Initialize the magnetometer command queue
    magnetometer_command_queue_handle =
        xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, magnetometer_command_queue_buffer, &magnetometer_mem.magnetometer_task_queue);
    if (magnetometer_command_queue_handle == NULL) {
        fatal("Failed to create magnetometer queue!\n");
    }

    while (true) {
        debug_impl("\n---------- Magnetometer Task Loop ----------\n");

        // Execute all commands contained in the queue
        if (xQueueReceive(magnetometer_command_queue_handle, &cmd, queue_block_time_ticks) == pdPASS) {
            do {
                debug("magnetometer: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_magnetometer(&cmd); // TODO
            } while (xQueueReceive(magnetometer_command_queue_handle, &cmd, 0) == pdPASS);
        }
        debug("magnetometer: No more commands queued.\n");

        enqueue_command(&cmd_checkin);
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
