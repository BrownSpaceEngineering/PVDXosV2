/**
 * arducam_main.c
 *
 * Main loop of the Arducam task: brings up the OV2640 camera over SPI and captures an
 * image over RTT each cycle. See arducam_driver.h.
 *
 * Created: October 2026
 * Authors: PVDX
 */

#include "logging.h"
#include "arducam_task.h"
#include "cmd_dispatcher.h"
#include "SEGGER_RTT.h"

// Arducam task memory structures
arducam_task_memory_t arducam_mem;

// Dedicated RTT up-buffer for streaming captured images on CAMERA_RTT_OUTPUT_CHANNEL (ch. 2).
static uint8_t arducam_rtt_image_buffer[SEGGER_RTT_IMAGE_BUFFER_SIZE];

/**
 * \fn main_arducam
 *
 * \param pvParameters a void pointer to the parameters required by the arducam task;
 *      not currently set by config
 *
 * \warning should never return
 */
void main_arducam(void *pvParameters) {
    info("arducam: Task Started!\n");

    pvdx_task_t *const current_task = get_current_task();
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);

    // Configure the image-streaming RTT up-channel (ch. 2); blocks if the host FIFO is full
    // so image bytes are not silently dropped.
    SEGGER_RTT_ConfigUpBuffer(CAMERA_RTT_OUTPUT_CHANNEL, "Image", arducam_rtt_image_buffer,
                              SEGGER_RTT_IMAGE_BUFFER_SIZE, SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL);

    status_t status = init_arducam_hardware();
    if (status != SUCCESS) {
        warning("arducam: init_arducam_hardware() failed (status=%d)\n", status);
    } else {
        info("arducam: initialized\n");
    }

    // Capture period; kept below the watchdog timeout/2 so the task still checks in on time.
    const TickType_t capture_interval_ticks = pdMS_TO_TICKS(3000);

    while (true) {
        debug("\n---------- Arducam Task Loop ----------\n");

        // Take a picture and stream it over RTT (channel 2)
        info("arducam: Capturing image...\n");
        capture_rtt();

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            while (enqueue_command(&cmd_checkin) != SUCCESS) {
                warning("arducam: Failed to enqueue watchdog checkin command\n");
            }
        }

        vTaskDelay(capture_interval_ticks);
    }
}
