/**
 * main.c
 *
 * The main entry point for PVDXos. This build initializes the hardware, verifies that the bootloader executed
 * successfully and runs every startup device check. It then waits 30 minutes, turns on the burn pin and idles
 * forever. The FreeRTOS scheduler is never started, so no tasks run.
 *
 * Created: November 20, 2023
 * Modified: November 9, 2025
 * Authors: Oren Kohavi, Siddharta Laloux, Tanish Makadia, Yi Liu,
 *          Defne Doken, Aidan Wang, Ignacio Blancas Rodriguez, Alexander Thaep
 */

#include "main.h"

#include "SEGGER_RTT.h"
#include "checks/device_checks.h"
#include "globals.h"
#include "logging.h"
#include "watchdog_driver.h"

#define BURN_WAIT_MINUTES 30

static status_t PVDX_init(void) {
    // Segger Buffer 0 is pre-configured at compile time according to segger documentation
    // Config the logging output channel (assuming it's not zero)
    if (LOGGING_RTT_OUTPUT_CHANNEL != 0) {
        SEGGER_RTT_ConfigUpBuffer(LOGGING_RTT_OUTPUT_CHANNEL, "Log Output", SEGGER_RTT_LOG_BUFFER, SEGGER_RTT_LOG_BUFFER_SIZE,
                                  SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    }
    return SUCCESS;
}

// Busy-waits one second at a time (delay_ms is fine here: the scheduler never runs, so nothing else owns SysTick).
// The watchdog task never runs either, so the hardware watchdog is normally off. It is fed every second anyway in
// case it is still running from an earlier boot.
static void wait_seconds_feeding_watchdog(uint32_t seconds) {
    for (uint32_t s = 0; s < seconds; s++) {
        watchdog_feed(WDT);
        delay_ms(1000);
    }
}

int main(void) {
    /* ---------- HARDWARE & LOGGING INITIALIZATION + BOOTLOADER CHECK ---------- */

    /* Initializes MCU, drivers and middleware */
    atmel_start_init();
    PVDX_init();
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

    /* ---------- DEVICE CHECKS ---------- */

    // Called outside info(): in RELEASE builds info() compiles out, which would skip the checks entirely
    bool at_least_one_failed = check_all_devices_on_startup();
    info("AT_LEAST_ONE_DEVICE_FAILED: %d\n", at_least_one_failed);
    (void)at_least_one_failed; // unused when info() compiles out

    /* ---------- WAIT, THEN BURN ---------- */

    info("Waiting %d minutes before turning on the burn pin\n", BURN_WAIT_MINUTES);
    for (uint32_t minute = 0; minute < BURN_WAIT_MINUTES; minute++) {
        wait_seconds_feeding_watchdog(60);
        info("%lu/%d minutes elapsed\n", (unsigned long)(minute + 1), BURN_WAIT_MINUTES);
    }

    gpio_set_pin_level(BURN, true);
    info("Burn pin (PB29) on\n");

    /* ---------- IDLE FOREVER (scheduler is never started) ---------- */

    while (true) {
        wait_seconds_feeding_watchdog(1);
    }
}
