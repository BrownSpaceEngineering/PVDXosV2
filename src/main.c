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

    status_t th_status = init_thermistors();
    if (th_status != SUCCESS) {
        warning("[!] init_thermistors() failed (status=%d)\n", th_status);
    } else {
        info("[+] Thermistors initialized\n");
    }

    // status_t phd_status = init_photodiodes();
    // if (phd_status != SUCCESS) {
    //     warning("[!] init_photodiodes() failed (status=%d)\n", phd_status);
    // } else {
    //     info("[+] Photodiodes initialized\n");
    // }

    /* ---------- THERMISTOR 0 VOLTAGE READOUT ---------- */

    // Read the raw ADC code for thermistor 0 and convert to the divider-node voltage.
    // Reads are ratiometric against VDDANA (~3.3 V), so V = code / full_scale * VREF.
    while (true) {
        uint16_t code = 0;
        status_t rs = therm_read_raw(0, &code);
        if (rs == SUCCESS) {
            float v = (float)code / THERM_ADC_FS * 3.3f;
            // Log mV (integer) to avoid relying on %f in the embedded printf.
            info("therm 0: code=%u V=%ld mV\n", code, (long)(v * 1000.0f));
        } else {
            warning("therm 0: read failed (status=%d)\n", rs);
        }

        // Drive the heaters from the voltage threshold (ON above 2.3 V = cold).
        heater_control_update();
        info("heaters: 1=%s 2=%s\n", heater_is_on(0) ? "ON" : "off", heater_is_on(1) ? "ON" : "off");

        delay_ms(1000);
    }
}
