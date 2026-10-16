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
    // Hardware-in-the-loop tests; then idle (skip the normal telemetry loop).
    tests_run();
    while (true) {
        delay_ms(1000);
    }
#endif

    /* ---------- FUEL GAUGE BRING-UP ---------- */

    // Probe only gauge addresses (and the mux, when enabled).
    fg_i2c_scan();

    status_t fg_status = init_fuel_gauges();
    if (fg_status != SUCCESS) {
        warning("[!] init_fuel_gauges() failed (status=%d)\n", fg_status);
    } else {
        info("[+] Fuel gauges initialized\n");
    }

    /* ---------- FUEL GAUGE TELEMETRY LOOP ---------- */

    // Read the configured packs once per second. Floats are printed as
    // integer milli-units to avoid relying on %f in the embedded printf.
    while (true) {
        fg_reading_t packs[FG_NUM_PACKS];
        status_t rs = fg_read_all(packs);
        if (rs != SUCCESS) {
            warning("fg_read_all failed (status=%d)\n", rs);
        }

        for (uint8_t ch = 0; ch < FG_NUM_PACKS; ch++) {
            fg_reading_t *p = &packs[ch];
            if (!p->valid) {
                warning("pack %u: telemetry unavailable (I2C read failed)\n", ch);
                continue;
            }
            info("pack %u: SOC=%ld m%%  Vpack=%ld mV  I=%ld mA  T=%ld mC\n", ch,
                 (long)(p->soc_pct * 1000.0f), (long)(p->pack_voltage * 1000.0f),
                 (long)(p->current_a * 1000.0f), (long)(p->temp_c * 1000.0f));
        }

        // Active-low ALRT line: if asserted, read status to identify the fault source.
        if (fg_alrt_asserted()) {
            warning("fuel gauge ALRT asserted - scanning\n");
            fg_scan_alerts();
        }

        delay_ms(1000);
    }
}
