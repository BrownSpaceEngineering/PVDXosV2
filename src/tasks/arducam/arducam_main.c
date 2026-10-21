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

    // Configure the image-streaming RTT up-channel (ch. 2).
#if defined(RELEASE)
    // No host drains channel 2 in flight, so a blocking channel would hang the task once the buffer fills
    // and trip the watchdog. Drop image bytes that don't fit instead.
    const unsigned rtt_image_mode = SEGGER_RTT_MODE_NO_BLOCK_SKIP;
#else
    // Block if the host FIFO is full so image bytes are not silently dropped.
    // NOTE: this requires a host reading channel 2; otherwise the task stalls and the watchdog resets the board.
    const unsigned rtt_image_mode = SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL;
#endif
    SEGGER_RTT_ConfigUpBuffer(CAMERA_RTT_OUTPUT_CHANNEL, "Image", arducam_rtt_image_buffer, SEGGER_RTT_IMAGE_BUFFER_SIZE,
                              rtt_image_mode);

    bool camera_ready = false;

    // Capture period; kept below the watchdog timeout/2 so the task still checks in on time.
    const TickType_t capture_interval_ticks = pdMS_TO_TICKS(3000);

    while (true) {
        debug("\n---------- Arducam Task Loop ----------\n");

        if (!camera_ready) {
            // (Re)initialize the camera; only capture once it has come up, since capture_rtt() needs a working camera
            status_t status = init_arducam_hardware();
            if (status != SUCCESS) {
                warning("arducam: init_arducam_hardware() failed (status=%d), retrying next cycle\n", status);
            } else {
                info("arducam: initialized\n");
                camera_ready = true;
            }
        } else {
            // Take a picture and stream it over RTT (channel 2)
            info("arducam: Capturing image...\n");
            status_t status = capture_rtt();
            if (status != SUCCESS) {
                warning("arducam: capture_rtt() failed (status=%d), reinitializing camera\n", status);
                camera_ready = false;
            }
        }

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            while (enqueue_command(&cmd_checkin) != SUCCESS) {
                warning("arducam: Failed to enqueue watchdog checkin command\n");
            }
        }

        vTaskDelay(capture_interval_ticks);
    }
}
