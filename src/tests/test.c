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

#include "fuel_gauge_driver.h"
#include "logging.h"

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

void tests_run(void) {
    test_fuel_gauge_hw();
    test_log("test results: %d/%d passed\n", tests_passed, tests_total);
}
