/**
 * magnetometer_task.c
 *
 * RTOS task wrapping the driver for the RM3100 magnetometer.
 *
 * Created: September 2026
 * Authors: PVDX
 */

#include "magnetometer_task.h"
#include "magnetometer_driver.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

// TODO: Add dispatchable functions here (e.g. on-demand read)

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn init_magnetometer
 *
 * \brief Initializes the magnetometer task
 *
 * \returns the command queue for the magnetometer
 */
QueueHandle_t init_magnetometer(void) {
    // Initialize the magnetometer command queue
    QueueHandle_t magnetometer_command_queue_handle =
        xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE,
                           magnetometer_mem.magnetometer_command_queue_buffer, &magnetometer_mem.magnetometer_task_queue);
    if (magnetometer_command_queue_handle == NULL) {
        fatal("Failed to create magnetometer queue!\n");
    }

    return magnetometer_command_queue_handle;
}
