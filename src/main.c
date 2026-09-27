/**
 * main.c
 *
 * Bare-metal entry point for the PVDX magnetometer debug build.
 *
 * This is a stripped-down harness for debugging the RM3100 magnetometer: there
 * is no FreeRTOS scheduler, no watchdog, and no other tasks. main() simply
 * brings up the hardware, initializes the RM3100 over SPI_DISPLAY
 * (SERCOM1: MOSI=PC22, SCK=PC23, MISO=PA18; CS=PB13), and then repeatedly reads
 * X/Y/Z and logs the values over RTT channel 1.
 *
 * Original OS authors: Oren Kohavi, Siddharta Laloux, Tanish Makadia, Yi Liu,
 * Defne Doken, Aidan Wang, Ignacio Blancas Rodriguez
 */

#include "main.h"

#include "globals.h"
#include "logging.h"
#include "magnetometer_driver.h"

// Time to wait between successive readings, in milliseconds
#define READ_INTERVAL_MS 500

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

    info("--- Bare-metal Magnetometer Debug Build ---\n");
    info("[+] Build Type: %s\n", BUILD_TYPE);
    info("[+] Build Date: %s\n", BUILD_DATE);
    info("[+] Build Time: %s\n", BUILD_TIME);
    info("[+] Built from branch: %s\n", GIT_BRANCH_NAME);
    info("[+] Built from commit: %s\n", GIT_COMMIT_HASH);

    /* ---------- MAGNETOMETER INITIALIZATION ---------- */

    status_t status = init_rm3100();
    if (status != SUCCESS) {
        warning("[!] init_rm3100() failed (status=%d)\n", status);
    } else {
        info("[+] RM3100 initialized (continuous mode)\n");
    }

    /* ---------- READ LOOP ---------- */

    while (true) {
        mag_raw_reading_t raw;
        mag_data_t adj;

        status_t rs = magnetometer_read(&raw, &adj);

        if (rs == SUCCESS) {
            // Raw counts are always safe to print. The gain-adjusted values are
            // floats; print their truncated integer part to avoid relying on
            // %f support in the embedded printf.
            info("mag raw:   x=%ld y=%ld z=%ld\n", (long)raw.x, (long)raw.y, (long)raw.z);
            info("mag adj:   x=%ld y=%ld z=%ld (integer part)\n", (long)adj.x, (long)adj.y, (long)adj.z);
        } else if (rs == ERROR_NOT_READY) {
            // STATUS DRDY bit clear: no new sample yet this cycle. Poll again shortly.
            debug("mag: not ready (STATUS DRDY clear)\n");
        } else {
            warning("[!] magnetometer_read() failed (status=%d)\n", rs);
        }

        delay_ms(READ_INTERVAL_MS);
    }
}
