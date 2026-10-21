/**
 * main.c
 *
 * Bare-metal entry point for the PVDX driver development build.
 *
 * This is a stripped-down harness for bringing up new drivers: there is no
 * FreeRTOS scheduler, no watchdog, and no other tasks. main() simply brings up
 * the hardware and logging, then exercises the Y-axis magnetorquer H-bridge
 * from 0% to 100% duty in both directions, stopping between sweeps.
 *
 * Original OS authors: Oren Kohavi, Siddharta Laloux, Tanish Makadia, Yi Liu,
 * Defne Doken, Aidan Wang, Ignacio Blancas Rodriguez
 */

#include "main.h"

#include "magnetorquer_driver.h"
#include "fuel_gauge_driver.h"
#include "globals.h"
#include "logging.h"
#include "tests/test.h"

static void PVDX_init(void) {
    // WARNING: Segger RTT channel 0 is pre-configured at compile time according to Segger documentation
    // Attempting to use channel 0 may result in errors.

    // Logging output channel (ch. 1)
    SEGGER_RTT_ConfigUpBuffer(
        LOGGING_RTT_OUTPUT_CHANNEL, "Log Output", SEGGER_RTT_LOG_BUFFER,
        SEGGER_RTT_LOG_BUFFER_SIZE, SEGGER_RTT_MODE_NO_BLOCK_SKIP
    );
}

int main(void) {
    /* ---------- HARDWARE & LOGGING INITIALIZATION ---------- */

    // Initializes MCU, drivers and middleware (clocks, pins, SERCOM SPI/I2C, delay timer)
    atmel_start_init();
    PVDX_init();

    info("--- Bare-metal Driver Development Build ---\n");
    info("[+] Build Type: %s\n", BUILD_TYPE);
    info("[+] Build Date: %s\n", BUILD_DATE);
    info("[+] Build Time: %s\n", BUILD_TIME);
    info("[+] Built from branch: %s\n", GIT_BRANCH_NAME);
    info("[+] Built from commit: %s\n", GIT_COMMIT_HASH);

#ifdef UNITTEST
    // Hardware-in-the-loop tests; then idle (skip the H-bridge bench loop).
    tests_run();
    while (true) {
        delay_ms(1000);
    }
#endif

#if FG_MUX_ONLY_TEST
    info("[+] Mux-only test: TCA9546A addr=0x%02x, %s\n", FG_MUX_ADDR, FG_I2C_DESCRIPTION);
    info("[+] No fuel-gauge reads or H-bridge exercise; downstream switch continuity is not tested\n");
    unsigned passes = 0, failures = 0;
    while (true) {
        status_t result = fg_mux_self_test();
        if (result == SUCCESS) {
            passes++;
            info("mux-only: cycle PASS (passes=%u failures=%u)\n", passes, failures);
        } else {
            failures++;
            warning("mux-only: cycle FAIL status=%d (passes=%u failures=%u)\n", result, passes, failures);
        }
        delay_ms(5000);
    }
#endif

    /* ---------- H-BRIDGE BENCH TEST ---------- */

    status_t mtq_status = init_magnetorquers();
    if (mtq_status != SUCCESS) {
        warning("[!] init_magnetorquers() failed (status=%d)\n", mtq_status);
        while (true) {
            delay_ms(1000);
        }
    }

    // Change this axis to MTQ_X or MTQ_Z to test another bridge.
    const mtq_axis_t test_axis = MTQ_Y;
    info("[+] H-bridge bench test: axis=%u, duty=0..100%%, step=10%%, hold=2s, stop=1s\n",
         (unsigned int)test_axis);

    while (true) {
        for (int direction = 1; direction >= -1; direction -= 2) {
            for (unsigned int percent = 0; percent <= 100; percent += 10) {
                float duty = (float)direction * (float)percent / 100.0f;
                info("MTQ: %s %u%%\n", direction > 0 ? "forward" : "reverse", percent);
                mtq_set(test_axis, duty);
                delay_ms(2000);
            }

            mtq_stop_all();
            info("MTQ: stopped\n");
            delay_ms(1000);
        }
    }
}
