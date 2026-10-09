/**
 * main.c
 *
 * The main entry point for PVDXos. This file initializes the hardware, verifies that the bootloader executed
 * successfully, creates the high-level OS integrity tasks along with the Cosmic Monkey task, and finally starts
 * the FreeRTOS scheduler.
 *
 * Created: November 20, 2023
 * Authors: Oren Kohavi, Siddharta Laloux, Tanish Makadia, Yi Liu, Defne Doken, Aidan Wang, Ignacio Blancas Rodriguez
 */

#include "main.h"

#include "checks/device_checks.h"
#include "globals.h"
#include "logging.h"
#include "magnetometer_driver.h"
#include "tests/test.h"

cosmic_monkey_task_arguments_t cm_args = {0};

static status_t PVDX_init(void) {
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

    return SUCCESS;
}

int main(void) {
    /* ---------- HARDWARE & LOGGING INITIALIZATION + BOOTLOADER CHECK ---------- */

    /* Initializes MCU, drivers and middleware */
    atmel_start_init();
    PVDX_init();
    // info_impl(RTT_CTRL_RESET RTT_CTRL_CLEAR); // Reset the terminal
    info("--- Atmel & Hardware Initialization Complete ---\n");
    info("[+] Build Type: %s\n", BUILD_TYPE);
    info("[+] Build Date: %s\n", BUILD_DATE);
    info("[+] Build Time: %s\n", BUILD_TIME);
    info("[+] Built from branch: %s\n", GIT_BRANCH_NAME);
    info("[+] Built from commit: %s\n", GIT_COMMIT_HASH);

    // Bootloader sets a magic number in backup RAM to indicate that it has run successfully
    uint32_t *p_magic_number = (uint32_t *)BOOTLOADER_MAGIC_NUMBER_ADDRESS;
    uint32_t magic_number = *p_magic_number;
    *p_magic_number = 0; // Clear the magic number so that this value doesn't linger
    if (magic_number == BOOTLOADER_MAGIC_NUMBER_VALUE) {
        info_impl("[+] Bootloader executed normally\n");
    } else {
        warning_impl("[!] Abnormal bootloader behavior (Magic Number: %x)\n", magic_number);
    }

    /* ---------- INIT WATCHDOG, COMMAND_DISPATCHER, TASK_MANAGER TASKS (in that order) ---------- */

    info("AT_LEAST_ONE_DEVICE_FAILED: %d\n", check_all_devices_on_startup());

    /* ---------- RM3100-ONLY BRING-UP (NO SCHEDULER) ---------- */

    // This build is dedicated to exercising the RM3100 magnetometer alone. We init the
    // sensor and poll it forever here in main(); the FreeRTOS scheduler is never started,
    // so no other PVDXos tasks run. Uses the ASF busy-wait delay_ms() (NOT vTaskDelay,
    // which requires the scheduler).
    status_t mag_status = init_rm3100();
    if (mag_status != SUCCESS) {
        warning("magnetometer: init_rm3100() failed (status=%d)\n", mag_status);
    } else {
        info("magnetometer: RM3100 initialized (continuous mode)\n");
    }

    while (true) {
        int32_t raw[3];
        float adj[3];
        status_t rs = magnetometer_read(raw, adj);
        if (rs == SUCCESS) {
            // Gain-adjusted values are floats; log their truncated integer part to avoid
            // relying on %f support in the embedded printf.
            info("magnetometer: raw x=%ld y=%ld z=%ld | adj(int) x=%ld y=%ld z=%ld\n", (long)raw[0], (long)raw[1],
                 (long)raw[2], (long)adj[0], (long)adj[1], (long)adj[2]);
        } else if (rs == ERROR_NOT_READY) {
            debug("magnetometer: data not ready this cycle\n");
        } else {
            warning("magnetometer: read failed (status=%d)\n", rs);
        }

        delay_ms(1000);
    }

    // Unreachable: this build never starts the scheduler.
}
