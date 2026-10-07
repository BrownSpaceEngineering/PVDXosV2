/**
 * src/tests/test.c
 *
 * On-target, hardware-in-the-loop tests. These are meant to pass ONLY when the real
 * fuel-gauge hardware (TCA9546A mux + four MAX17205 gauges on live 2S packs) is attached:
 * the ASF I2C HAL returns an error on a NACK, so every fg_* call that returns SUCCESS
 * already proves a device ACK'd on the bus. Disconnect the harness and these fail.
 *
 * Built with `make test` (UNITTEST); results print over RTT ch.1 as "test results: N/N".
 */

#include "tests/test.h"

#include <math.h>
#include <stdlib.h>

#include "fuel_gauge_driver.h"
#include "logging.h"
#include "magnetorquer_driver.h"
#include "photodiode_driver.h"
#include "thermistor_driver.h"

int tests_passed = 0;
int tests_total = 0;

// ---- Plausible-readout thresholds (tune for the bench/flight pack) ----
#define FG_TEST_V_MIN        4.0f  // 2S pack floor (V)
#define FG_TEST_V_MAX        9.0f  // 2S pack ceiling (V), above full charge (8.4V)
#define FG_TEST_T_MIN        (-40.0f) // degC (sensor operating range)
#define FG_TEST_T_MAX        85.0f    // degC
#define FG_TEST_I_ABS_MAX    6.0f     // A; > full-scale for Rsense=0.01 (~5.12A)
#define FG_TEST_CELL_SUM_TOL 0.3f     // V; |Cell1+Cell2 - pack| tolerance

static void test_fuel_gauge_hw(void) {
    test_log("----- testing fuel gauge (hardware) -----\n");

    // Startup: requires the bus to come up and the gauges to respond.
    PVDX_ASSERT_MSG(init_fuel_gauges() == SUCCESS, "init_fuel_gauges failed - board connected?\n");

    for (uint8_t ch = 0; ch < FG_NUM_PACKS; ch++) {
        test_log("--- gauge %u (channel %u) ---\n", ch + 1, ch);

        // Mux responds + switches to this channel, and a real gauge ACKs with sane
        // identity data (DevName is never all-zeros or all-ones on a live part).
        uint16_t devname = 0;
        status_t id_rc = fg_read_raw(ch, MAX17205_REG_DEVNAME, &devname);
        PVDX_ASSERT_MSG(id_rc == SUCCESS, "mux/gauge did not respond (DevName read)\n");
        test_log("DevName = 0x%04x\n", devname);
        PVDX_ASSERT_MSG(devname != 0x0000 && devname != 0xFFFF, "DevName looks like a dead/floating bus\n");

        // Telemetry read must succeed (again, hardware-gated).
        fg_reading_t r;
        status_t rd_rc = fg_read_pack(ch, &r);
        PVDX_ASSERT_MSG(rd_rc == SUCCESS, "fg_read_pack failed\n");
        if (rd_rc != SUCCESS) {
            continue; // skip the value checks if the read itself failed
        }

        test_log("SOC=%ld%% V=%ld mV I=%ld mA T=%ld C\n", (long)r.soc_pct, (long)(r.pack_voltage * 1000.0f),
                 (long)(r.current_a * 1000.0f), (long)r.temp_c);

        // Physically plausible readouts (ranges only - never float == under -Wfloat-equal).
        PVDX_ASSERT_MSG(r.soc_pct >= 0.0f && r.soc_pct <= 100.0f, "SOC out of [0,100]%\n");
        PVDX_ASSERT_MSG(r.pack_voltage >= FG_TEST_V_MIN && r.pack_voltage <= FG_TEST_V_MAX, "pack voltage implausible for 2S\n");
        PVDX_ASSERT_MSG(r.temp_c >= FG_TEST_T_MIN && r.temp_c <= FG_TEST_T_MAX, "temperature out of range\n");
        PVDX_ASSERT_MSG(fabsf(r.current_a) <= FG_TEST_I_ABS_MAX, "current beyond full scale\n");

        // Coherence: the two cell voltages should sum to roughly the pack voltage. Random
        // noise / a garbage bus will not satisfy this - only a real 2S stack.
        uint16_t c1 = 0, c2 = 0;
        status_t c1_rc = fg_read_raw(ch, MAX17205_REG_CELL1, &c1);
        status_t c2_rc = fg_read_raw(ch, MAX17205_REG_CELL2, &c2);
        PVDX_ASSERT_MSG(c1_rc == SUCCESS && c2_rc == SUCCESS, "cell voltage read failed\n");
        if (c1_rc == SUCCESS && c2_rc == SUCCESS) {
            float cell_sum = (float)c1 * MAX17205_CELL_LSB_V + (float)c2 * MAX17205_CELL_LSB_V;
            test_log("Cell1+Cell2 = %ld mV (pack %ld mV)\n", (long)(cell_sum * 1000.0f), (long)(r.pack_voltage * 1000.0f));
            PVDX_ASSERT_MSG(fabsf(cell_sum - r.pack_voltage) <= FG_TEST_CELL_SUM_TOL, "Cell1+Cell2 != pack voltage\n");
        }
    }

    // ALRT (shared, active-low): in a healthy state no gauge is alarming, so the line
    // should read deasserted. Weak check - confirms the pull-up and idle state, not the
    // full alert path (which would need threshold programming to provoke).
    PVDX_ASSERT_MSG(!fg_alrt_asserted(), "ALRT asserted at idle (a gauge is alarming, or pin stuck low)\n");
}

// ---- Thermistor plausible-reading band (tune for the bench environment) ----
#define THERM_TEST_C_MIN (-20.0f)
#define THERM_TEST_C_MAX 60.0f

static void test_thermistor_hw(void) {
    test_log("----- testing thermistors + heaters (hardware) -----\n");

    PVDX_ASSERT_MSG(init_thermistors() == SUCCESS, "init_thermistors failed\n");

    for (uint8_t i = 0; i < THERM_COUNT; i++) {
        // A disconnected/open thermistor rails the ADC to 0 or full-scale; a strict
        // in-range code is the "only passes when connected" gate.
        uint16_t code = 0;
        status_t raw_rc = therm_read_raw(i, &code);
        PVDX_ASSERT_MSG(raw_rc == SUCCESS, "therm_read_raw failed\n");
        test_log("therm %u: code=%u\n", i, code);
        PVDX_ASSERT_MSG(code > 0 && (float)code < THERM_ADC_FS, "ADC code railed - thermistor open/short?\n");

        float t = 0.0f;
        status_t t_rc = therm_read_celsius(i, &t);
        PVDX_ASSERT_MSG(t_rc == SUCCESS, "therm_read_celsius failed\n");
        test_log("therm %u: %ld C\n", i, (long)t);
        PVDX_ASSERT_MSG(t >= THERM_TEST_C_MIN && t <= THERM_TEST_C_MAX, "temperature implausible\n");
    }

    // Tie the control output to live readings: at normal lab ambient (> setpoint+hyst = 6C)
    // both heaters must settle OFF. (Assumes the bench is warmer than the heater band.)
    heater_control_update();
    PVDX_ASSERT_MSG(!heater_is_on(0) && !heater_is_on(1), "heater ON at warm ambient (check wiring/polarity)\n");
}

// ---- Photodiode test (ambient-light dependent; tune for the bench) ----
#define PHD_TEST_LIGHT_FLOOR 50 // ADC code a lit channel should exceed
#define PHD_TEST_MIN_LIT     1  // at least this many channels above the floor

static void test_photodiode_hw(void) {
    test_log("----- testing photodiodes (hardware) -----\n");

    PVDX_ASSERT_MSG(init_photodiodes() == SUCCESS, "init_photodiodes failed\n");

    uint16_t codes[PHD_COUNT];
    PVDX_ASSERT_MSG(phd_read_all(codes) == SUCCESS, "phd_read_all failed\n");

    uint16_t lo = 0xFFFF, hi = 0;
    int lit = 0;
    for (uint8_t i = 0; i < PHD_COUNT; i++) {
        test_log("phd %u: code=%u\n", i, codes[i]);
        if (codes[i] > PHD_TEST_LIGHT_FLOOR) {
            lit++;
        }
        if (codes[i] < lo) {
            lo = codes[i];
        }
        if (codes[i] > hi) {
            hi = codes[i];
        }
    }

    // A live, illuminated array reads nonzero on some channels and shows spread across them;
    // a dead/disconnected array reads a flat floor. Assumes bench ambient light.
    PVDX_ASSERT_MSG(lit >= PHD_TEST_MIN_LIT, "no photodiode above light floor (dark or disconnected?)\n");
    PVDX_ASSERT_MSG(hi != lo, "all photodiodes identical (array not responding?)\n");
}

// ---- Magnetorquer test: verify the PWM/control path at the TCC register level ----
// The MCU can't sense coil current, so we read back the compare registers and the nSLEEP
// pin after each command. (True actuation needs a scope or the magnetometer.)
static void check_mtq_axis(const char *name, mtq_axis_t axis, Tcc *hw, uint8_t cc_in1, uint8_t cc_in2,
                           uint32_t slp_pin) {
    uint32_t per = hri_tcc_get_PER_reg(hw, 0xFFFFFFFF);
    uint32_t half = (uint32_t)(0.5f * (float)per + 0.5f);

    test_log("--- MTQ %s (PER=%lu) ---\n", name, (unsigned long)per);

    // +50%: IN1 ~half duty, IN2 off, awake.
    mtq_set(axis, 0.5f);
    PVDX_ASSERT_MSG(labs((long)hri_tcc_read_CC_reg(hw, cc_in1) - (long)half) <= 2, "+duty: CC(IN1) wrong\n");
    PVDX_ASSERT_MSG(hri_tcc_read_CC_reg(hw, cc_in2) == 0, "+duty: CC(IN2) not zero\n");
    PVDX_ASSERT_MSG(gpio_get_pin_level(slp_pin), "+duty: SLP not awake\n");

    // -50%: IN1 off, IN2 ~half duty, awake.
    mtq_set(axis, -0.5f);
    PVDX_ASSERT_MSG(hri_tcc_read_CC_reg(hw, cc_in1) == 0, "-duty: CC(IN1) not zero\n");
    PVDX_ASSERT_MSG(labs((long)hri_tcc_read_CC_reg(hw, cc_in2) - (long)half) <= 2, "-duty: CC(IN2) wrong\n");
    PVDX_ASSERT_MSG(gpio_get_pin_level(slp_pin), "-duty: SLP not awake\n");

    // Stop: both off, asleep.
    mtq_stop(axis);
    PVDX_ASSERT_MSG(hri_tcc_read_CC_reg(hw, cc_in1) == 0 && hri_tcc_read_CC_reg(hw, cc_in2) == 0, "stop: CC not zero\n");
    PVDX_ASSERT_MSG(!gpio_get_pin_level(slp_pin), "stop: SLP not asleep\n");
}

static void test_magnetorquer(void) {
    test_log("----- testing magnetorquers (register readback) -----\n");

    PVDX_ASSERT_MSG(init_magnetorquers() == SUCCESS, "init_magnetorquers failed\n");

    check_mtq_axis("X", MTQ_X, TCC0, 0, 1, MTQ_X_SLP);
    check_mtq_axis("Y", MTQ_Y, TCC0, 2, 3, MTQ_Y_SLP);
    check_mtq_axis("Z", MTQ_Z, TCC1, 0, 1, MTQ_Z_SLP);

    mtq_stop_all();
}

void tests_run(void) {
    test_fuel_gauge_hw();
    test_thermistor_hw();
    test_photodiode_hw();
    test_magnetorquer();
    test_log("test results: %d/%d passed\n", tests_passed, tests_total);
}
