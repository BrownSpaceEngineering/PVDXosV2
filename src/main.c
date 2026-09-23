/**
 * main.c
 *
 * Bare-metal entry point for the PVDX camera debug build.
 *
 * This is a stripped-down harness for debugging the Arducam OV2640: there is no
 * FreeRTOS scheduler, no watchdog, and no other tasks. main() simply brings up
 * the hardware, initializes the camera, and then repeatedly captures a frame and
 * streams it over RTT channel 2 (see capture_rtt() in arducam_driver.c). Log
 * output goes over RTT channel 1.
 *
 * Original OS authors: Oren Kohavi, Siddharta Laloux, Tanish Makadia, Yi Liu,
 * Defne Doken, Aidan Wang, Ignacio Blancas Rodriguez
 */

#include "main.h"

#include "arducam_driver.h"
#include "globals.h"
#include "logging.h"

// Time to wait between successive captures, in milliseconds
#define CAPTURE_INTERVAL_MS 1000

static void PVDX_init(void) {
    // WARNING: Segger RTT channel 0 is pre-configured at compile time according to Segger documentation
    // Attempting to use channel 0 may result in errors.

    // Logging output channel (ch. 1)
    SEGGER_RTT_ConfigUpBuffer(
        LOGGING_RTT_OUTPUT_CHANNEL, "Log Output", SEGGER_RTT_LOG_BUFFER,
        SEGGER_RTT_LOG_BUFFER_SIZE, SEGGER_RTT_MODE_NO_BLOCK_SKIP
    );

    // Image streaming channel (ch. 2)
    SEGGER_RTT_ConfigUpBuffer(
        CAMERA_RTT_OUTPUT_CHANNEL, "Image", SEGGER_RTT_IMAGE_BUFFER,
        SEGGER_RTT_IMAGE_BUFFER_SIZE, SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL
    );
}

int main(void) {
    /* ---------- HARDWARE & LOGGING INITIALIZATION ---------- */

    // Initializes MCU, drivers and middleware (clocks, pins, SERCOM SPI/I2C, delay timer)
    atmel_start_init();
    PVDX_init();

    info("--- Bare-metal Camera Debug Build ---\n");
    info("[+] Build Type: %s\n", BUILD_TYPE);
    info("[+] Build Date: %s\n", BUILD_DATE);
    info("[+] Build Time: %s\n", BUILD_TIME);
    info("[+] Built from branch: %s\n", GIT_BRANCH_NAME);
    info("[+] Built from commit: %s\n", GIT_COMMIT_HASH);

    /* ---------- CAMERA INITIALIZATION ---------- */

    status_t status = init_arducam_hardware();
    if (status != SUCCESS) {
        warning("[!] init_arducam_hardware() failed (status=%d)\n", status);
    }

    /* ---------- CAPTURE LOOP ---------- */

    while (true) {
        info("--- Capturing frame ---\n");
        capture_rtt();
        delay_ms(CAPTURE_INTERVAL_MS);
    }
}
