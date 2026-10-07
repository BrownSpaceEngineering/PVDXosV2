/**
 * main.c
 *
 * Bare-metal entry point for the PVDX driver development build.
 *
 * This is a stripped-down harness for bringing up new drivers: there is no
 * FreeRTOS scheduler, no watchdog, and no other tasks. main() simply brings up
 * the hardware and logging, then drops into an idle loop. Add driver init and
 * exercise code as needed.
 *
 * Original OS authors: Oren Kohavi, Siddharta Laloux, Tanish Makadia, Yi Liu,
 * Defne Doken, Aidan Wang, Ignacio Blancas Rodriguez
 */

#include "main.h"

#include "fuel_gauge_driver.h"
#include "globals.h"
#include "logging.h"
#include "magnetorquer_driver.h"
#include "photodiode_driver.h"
#include "tests/test.h"
#include "thermistor_driver.h"

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
    // Hardware-in-the-loop tests; then idle (skip the normal telemetry loop).
    tests_run();
    while (true) {
        delay_ms(1000);
    }
#endif

    /* ---------- DRIVER BRING-UP ---------- */

    status_t fg_status = init_fuel_gauges();
    if (fg_status != SUCCESS) {
        warning("[!] init_fuel_gauges() failed (status=%d)\n", fg_status);
    } else {
        info("[+] Fuel gauges initialized\n");
    }

    status_t th_status = init_thermistors();
    if (th_status != SUCCESS) {
        warning("[!] init_thermistors() failed (status=%d)\n", th_status);
    } else {
        info("[+] Thermistors initialized\n");
    }

    status_t phd_status = init_photodiodes();
    if (phd_status != SUCCESS) {
        warning("[!] init_photodiodes() failed (status=%d)\n", phd_status);
    } else {
        info("[+] Photodiodes initialized\n");
    }

    // Magnetorquers are brought up but left stopped - never fire actuators uncommanded.
    status_t mtq_status = init_magnetorquers();
    if (mtq_status != SUCCESS) {
        warning("[!] init_magnetorquers() failed (status=%d)\n", mtq_status);
    } else {
        info("[+] Magnetorquers initialized (stopped)\n");
    }

    /* ---------- READ LOOP ---------- */

    while (true) {
        fg_reading_t packs[FG_NUM_PACKS];
        fg_read_all(packs);

        for (int i = 0; i < FG_NUM_PACKS; i++) {
            // Values are floats; print the truncated integer part to avoid relying on
            // %f support in the embedded printf.
            info("pack %d: SOC=%ld%% V=%ld mV I=%ld mA T=%ld C\n", i + 1,
                 (long)packs[i].soc_pct, (long)(packs[i].pack_voltage * 1000.0f),
                 (long)(packs[i].current_a * 1000.0f), (long)packs[i].temp_c);
        }

        if (fg_alrt_asserted()) {
            warning("[!] fuel-gauge ALRT asserted - scanning\n");
            fg_scan_alerts();
        }

        // Thermistors + closed-loop heater control.
        float temps[THERM_COUNT];
        therm_read_all(temps);
        for (int i = 0; i < THERM_COUNT; i++) {
            info("therm %d: %ld C\n", i, (long)temps[i]);
        }
        heater_control_update();
        info("heaters: 1=%s 2=%s\n", heater_is_on(0) ? "ON" : "off", heater_is_on(1) ? "ON" : "off");

        // Photodiodes (coarse sun sensing).
        for (int i = 0; i < PHD_COUNT; i++) {
            float v = 0.0f;
            phd_read_voltage(i, &v);
            info("phd %2d: %ld mV\n", i, (long)(v * 1000.0f));
        }

        // Magnetorquers: left stopped. Example (commented) command:
        //   mtq_set(MTQ_Z, 0.5f); // +50%% dipole on Z

        delay_ms(1000);
    }
}
