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

// The SERCOM6 I2C pins: PD09 = SDA (PAD0), PD08 = SCL (PAD1).
#define PIN_SDA GPIO(GPIO_PORTD, 9)
#define PIN_SCL GPIO(GPIO_PORTD, 8)

// Continuity/bring-up aid: drives PD09/PD08 as plain GPIO so each line can be traced with a
// multimeter to the gauge's SDA/SCL pins. Overrides the I2C pinmux and LOOPS FOREVER, so it
// runs instead of normal bring-up. Watch RTT ch.1 to know the current state, or set a
// breakpoint and single-step. Expected at the gauge pin: HIGH ~= 3.3 V, LOW ~= 0 V.
// Remove the call in main() to return to normal boot.
// static void pin_toggle_test(void) {
//     gpio_set_pin_function(PIN_SDA, GPIO_PIN_FUNCTION_OFF);
//     gpio_set_pin_function(PIN_SCL, GPIO_PIN_FUNCTION_OFF);
//     gpio_set_pin_direction(PIN_SDA, GPIO_DIRECTION_OUT);
//     gpio_set_pin_direction(PIN_SCL, GPIO_DIRECTION_OUT);

//     info("[PIN TEST] Driving PD09(SDA)/PD08(SCL) as GPIO. Probe at the gauge's SDA/SCL pins.\n");

//     while (true) {
//         gpio_set_pin_level(PIN_SDA, true);
//         gpio_set_pin_level(PIN_SCL, false);
//         info("[PIN TEST] PD09(SDA)=HIGH  PD08(SCL)=LOW\n");
//         delay_ms(3000);

//         gpio_set_pin_level(PIN_SDA, false);
//         gpio_set_pin_level(PIN_SCL, true);
//         info("[PIN TEST] PD09(SDA)=LOW   PD08(SCL)=HIGH\n");
//         delay_ms(3000);

//         gpio_set_pin_level(PIN_SDA, true);
//         gpio_set_pin_level(PIN_SCL, true);
//         info("[PIN TEST] PD09(SDA)=HIGH  PD08(SCL)=HIGH\n");
//         delay_ms(3000);

//         gpio_set_pin_level(PIN_SDA, false);
//         gpio_set_pin_level(PIN_SCL, false);
//         info("[PIN TEST] PD09(SDA)=LOW   PD08(SCL)=LOW\n");
//         delay_ms(3000);
//     }
// }

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

    // Pin continuity probe (loops forever). Remove this call to resume normal boot.
    // pin_toggle_test();

#ifdef UNITTEST
    // Hardware-in-the-loop tests; then idle (skip the normal telemetry loop).
    tests_run();
    while (true) {
        delay_ms(1000);
    }
#endif

    /* ---------- DRIVER BRING-UP ---------- */

    // fg_i2c_scan();

    // status_t fg_status = init_fuel_gauges();
    // if (fg_status != SUCCESS) {
    //     warning("[!] init_fuel_gauges() failed (status=%d)\n", fg_status);
    // } else {
    //     info("[+] Fuel gauges initialized\n");
    // }

    // status_t th_status = init_thermistors();
    // if (th_status != SUCCESS) {
    //     warning("[!] init_thermistors() failed (status=%d)\n", th_status);
    // } else {
    //     info("[+] Thermistors initialized\n");
    // }

    // status_t phd_status = init_photodiodes();
    // if (phd_status != SUCCESS) {
    //     warning("[!] init_photodiodes() failed (status=%d)\n", phd_status);
    // } else {
    //     info("[+] Photodiodes initialized\n");
    // }

    status_t mtq_status = init_magnetorquers();
    if (mtq_status != SUCCESS) {
        warning("[!] init_magnetorquers() failed (status=%d)\n", mtq_status);
    } else {
        info("[+] Magnetorquers initialized (stopped)\n");
    }

    /* ---------- MAGNETORQUER SWEEP LOOP ---------- */

    // Sweep the X magnetorquer duty through a smooth triangle: 0 -> +1 -> 0 -> -1 -> 0,
    // stepping in 0.1 increments with a short dwell at each step. Duty is tracked in
    // integer tenths to avoid float-equality comparisons in the loop conditions.
    static const int sweep_waypoints[] = {0, 10, 0, -10, 0}; // duty * 10
    const int num_waypoints = (int)(sizeof(sweep_waypoints) / sizeof(sweep_waypoints[0]));
    const uint16_t step_dwell_ms = 200; // time spent at each 0.1 step

    while (true) {
        for (int w = 0; w < num_waypoints - 1; w++) {
            int from = sweep_waypoints[w];
            int to = sweep_waypoints[w + 1];
            int dir = (to > from) ? 1 : -1;

            // Walk from the current waypoint toward the next, hitting each 0.1 step.
            for (int tenths = from; tenths != to; tenths += dir) {
                mtq_set(MTQ_X, (float)tenths / 10.0f);
                info("mtq X duty = %ld/10\n", (long)tenths);
                delay_ms(step_dwell_ms);
            }
        }
    }
}
