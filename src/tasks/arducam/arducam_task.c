/**
 * arducam_task.c
 *
 * RTOS task wrapping the driver for the Arducam OV2640 camera.
 *
 * Created: November 17, 2024 4:26 AM
 * Authors: Alexander Thaep, Tanish Makadia, Zach Mahan
 */

#include "arducam_task.h"
#include "arducam_driver.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

// TODO: Add dispatchable functions here (e.g. capture_image)

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn init_arducam
 * 
 * \brief Initializes the arducam task
 *
 * \returns the command queue for the arducam
 */
QueueHandle_t init_arducam(void) {
    // Initialize the arducam hardware
    status_t status = init_arducam_hardware();

    fatal_on_error(status, "Failed to initialize arducam hardware!\n");

    // Initialize the arducam command queue
    QueueHandle_t arducam_command_queue_handle = xQueueCreateStatic(
        COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, arducam_mem.arducam_command_queue_buffer, &arducam_mem.arducam_task_queue);
    if (arducam_command_queue_handle == NULL) {
        fatal("Failed to create arducam queue!\n");
    }

    return arducam_command_queue_handle;
}
