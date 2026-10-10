/**
 * test_pmb.c
 *
 * On-board test of the PMB measurement chain: ADG734 pixel routing (via the MCP23017), PVD_CRNT DAC and INA226.
 * First a sanity stage checks that every device is present, initializes and responds correctly; only if it
 * passes, every pixel is swept from 0 to full scale and the INA226 bus voltage is checked against the commanded
 * value. Requires the PMB hardware; results are reported over RTT.
 */

#include "tests/test_pmb.h"

#include "drivers/pmb/adg734.h"
#include "drivers/pmb/ina226.h"
#include "drivers/pmb/mcp23017.h"
#include "drivers/pmb/pmb_i2c.h"
#include "drivers/pmb/pvd_dac.h"
#include "drivers/watchdog/watchdog_driver.h"
#include "hal_delay.h"
#include "logging.h"
#include "tests/test.h"

#define PMB_TEST_STEP_MV 50U                                              // Pixel voltage step
#define PMB_TEST_TOLERANCE_MV 50                                          // Allowed |measured - commanded| pixel voltage
#define PMB_TEST_SETTLE_MS 5U                                             // Opamp settling time after a DAC step
#define PMB_TEST_CONVERSION_TIMEOUT_MS (4U * INA226_CONVERSION_PERIOD_MS) // Two conversions plus margin
#define PMB_TEST_CHAIN_MIN_SWING_MV (PVD_PIXEL_FULL_SCALE_MV / 2)         // Minimum response to a 0 -> full-scale step

// Asserts like PVDX_ASSERT_MSG and also counts the failure, so the sanity stage can gate the sweep
#define PMB_CHECK(failures, cond, msg)                                                                                                     \
    do {                                                                                                                                   \
        bool pmb_check_ok = (cond);                                                                                                        \
        PVDX_ASSERT_MSG(pmb_check_ok, msg);                                                                                                \
        if (!pmb_check_ok) {                                                                                                               \
            (failures)++;                                                                                                                  \
        }                                                                                                                                  \
    } while (0)

#if defined(UNITTEST)
static void test_pmb_sweep_pixel(uint8_t pixel) {
    PVDX_ASSERT_MSG(adg734_select_pixel(pixel) == SUCCESS, "pmb select pixel\n");
    uint8_t selected = PMB_PIXEL_NONE;
    PVDX_ASSERT_MSG(adg734_read_selected(&selected) == SUCCESS && selected == pixel, "pmb pixel select readback\n");

    int out_of_tolerance = 0;
    for (uint32_t target_mv = 0; target_mv <= PVD_PIXEL_FULL_SCALE_MV; target_mv += PMB_TEST_STEP_MV) {
        watchdog_feed(WDT);

        ina226_measurement_t m = {0};
        status_t status = pvd_dac_set_pixel_mv(target_mv);
        if (status == SUCCESS) {
            delay_ms(PMB_TEST_SETTLE_MS);
            status = ina226_wait_for_fresh_conversion(PMB_TEST_CONVERSION_TIMEOUT_MS);
        }
        if (status == SUCCESS) {
            status = ina226_read_measurement(&m);
        }
        PVDX_ASSERT_MSG(status == SUCCESS, "pmb sweep step comms\n");
        if (status != SUCCESS) {
            test_log("px %2u  set %4u mV  status %d\n", pixel, (unsigned)target_mv, status);
            continue;
        }

        // The opamp regulates FOLLOW, so that is what must match the command; the pixel differs by the shunt drop
        int follow_mv = (int)(m.follow_uv / 1000);
        int pixel_mv = (int)(m.bus_uv / 1000U);
        int error_mv = follow_mv - (int)target_mv;
        test_log("px %2u  set %4u mV  follow %4d mV  err %4d mV  pixel %4d mV  I %8d nA  P %7u nW\n", pixel, (unsigned)target_mv, follow_mv,
                 error_mv, pixel_mv, (int)m.current_na, (unsigned)m.power_nw);

        bool in_tolerance = error_mv <= PMB_TEST_TOLERANCE_MV && error_mv >= -PMB_TEST_TOLERANCE_MV;
        PVDX_ASSERT_MSG(in_tolerance, "pmb opamp output tracks DAC\n");
        if (!in_tolerance) {
            out_of_tolerance++;
        }
    }
    test_log("px %2u  done, %d steps out of tolerance\n", pixel, out_of_tolerance);
}

// Settles, waits for a conversion that reflects the current input, and reads the opamp output voltage (-1 on failure)
static status_t test_pmb_measure_mv(int *measured_mv) {
    delay_ms(PMB_TEST_SETTLE_MS);
    status_t status = ina226_wait_for_fresh_conversion(PMB_TEST_CONVERSION_TIMEOUT_MS);
    ina226_measurement_t m = {0};
    if (status == SUCCESS) {
        status = ina226_read_measurement(&m);
    }
    *measured_mv = (status == SUCCESS) ? (int)(m.follow_uv / 1000) : -1;
    return status;
}

// Lists every address that acknowledges on the PMB bus; anything other than the INA226 and MCP23017 is unexpected
static void test_pmb_scan_bus(void) {
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (pmb_i2c_probe(addr) == SUCCESS) {
            test_log("pmb: device at 0x%02x%s\n", addr,
                     (addr == INA226_I2C_ADDR)         ? " (INA226)"
                         : (addr == MCP23017_I2C_ADDR) ? " (MCP23017)"
                                                       : " (unexpected)");
        }
    }
}

/**
 * Checks each device and link on its own, in dependency order, so a failure points at one part:
 * I2C bus -> INA226 -> MCP23017 -> ADG734 routing -> DAC -> DAC-to-INA226 measurement chain.
 * Returns the number of failed checks.
 */
static int test_pmb_sanity(void) {
    int failures = 0;
    test_log("--- pmb sanity checks ---\n");

    // I2C bus and device presence
    PMB_CHECK(failures, pmb_i2c_init() == SUCCESS, "pmb i2c bus enable\n");
    test_pmb_scan_bus();
    PMB_CHECK(failures, pmb_i2c_probe(INA226_I2C_ADDR) == SUCCESS, "pmb ina226 acks at 0x40\n");
    PMB_CHECK(failures, pmb_i2c_probe(MCP23017_I2C_ADDR) == SUCCESS, "pmb mcp23017 acks at 0x20\n");

    // INA226: identity, init, register readback and a live conversion
    uint16_t value = 0;
    PMB_CHECK(failures, ina226_read_reg(INA226_MANUFACTURER_ID, &value) == SUCCESS && value == INA226_MFG_ID,
              "pmb ina226 manufacturer id\n");
    PMB_CHECK(failures, ina226_read_reg(INA226_DIE_ID, &value) == SUCCESS && (value & 0xFFF0U) == INA226_DIE_ID_VALUE,
              "pmb ina226 die id\n");
    PMB_CHECK(failures, ina226_init() == SUCCESS, "pmb ina226 init\n");
    PMB_CHECK(failures, ina226_read_reg(INA226_CONFIG, &value) == SUCCESS && value == INA226_DEFAULT_CONFIG,
              "pmb ina226 config readback\n");
    PMB_CHECK(failures, ina226_read_reg(INA226_CALIBRATION, &value) == SUCCESS && value == INA226_CAL_VALUE,
              "pmb ina226 calibration readback\n");
    PMB_CHECK(failures, ina226_wait_for_fresh_conversion(PMB_TEST_CONVERSION_TIMEOUT_MS) == SUCCESS, "pmb ina226 converting\n");

    // ADG734 IN lines float until the MCP23017 drives them, so adg734_init() (which also brings up the MCP23017)
    // runs before anything else touches the expander
    PMB_CHECK(failures, adg734_init() == SUCCESS, "pmb adg734 init\n");

    // MCP23017: a write/readback pattern on DEFVALA/B (inert while GPINTEN = 0) to exercise both directions
    const uint16_t patterns[] = {0xA55AU, 0x5AA5U, 0x0000U};
    for (unsigned i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        uint16_t readback = (uint16_t)~patterns[i];
        PMB_CHECK(failures,
                  mcp23017_write_reg16(MCP23017_DEFVALA, patterns[i]) == SUCCESS &&
                      mcp23017_read_reg16(MCP23017_DEFVALA, &readback) == SUCCESS && readback == patterns[i],
                  "pmb mcp23017 register write/readback\n");
    }

    // ADG734 routing: all IN pins outputs, nothing selected, then each pixel alone. Selection is read from the
    // MCP23017 pin levels, so a stuck or shorted IN line shows up as the wrong pixel or two pixels at once (the
    // switches themselves are only proven by current through the pixel).
    uint16_t iodir = 0xFFFF;
    PMB_CHECK(failures, mcp23017_read_reg16(MCP23017_IODIRA, &iodir) == SUCCESS && iodir == MCP23017_ALL_OUTPUTS,
              "pmb adg734 IN pins are outputs\n");
    uint8_t selected = 0;
    PMB_CHECK(failures, adg734_read_selected(&selected) == SUCCESS && selected == PMB_PIXEL_NONE, "pmb no pixel selected after init\n");
    for (uint8_t pixel = 0; pixel < PMB_PIXEL_COUNT; pixel++) {
        selected = PMB_PIXEL_NONE;
        bool ok = adg734_select_pixel(pixel) == SUCCESS && adg734_read_selected(&selected) == SUCCESS && selected == pixel;
        if (!ok) {
            test_log("pmb: selecting pixel %u read back %u\n", pixel, selected);
        }
        PMB_CHECK(failures, ok, "pmb adg734 select each pixel\n");
    }
    PMB_CHECK(failures, adg734_deselect_all() == SUCCESS && adg734_read_selected(&selected) == SUCCESS && selected == PMB_PIXEL_NONE,
              "pmb adg734 deselect all\n");

    // DAC: channel up and accepting writes
    PMB_CHECK(failures, pvd_dac_init() == SUCCESS, "pmb dac init\n");
    PMB_CHECK(failures, pvd_dac_is_ready(), "pmb dac ready\n");

    // Measurement chain: at 0 V the opamp output should sit near 0; a full-scale step on pixel 0 must move it by at
    // least half of full scale (DAC -> divider -> opamp -> shunt -> INA226)
    int zero_mv = -1, full_mv = -1;
    PMB_CHECK(failures, pvd_dac_set_pixel_mv(0) == SUCCESS && test_pmb_measure_mv(&zero_mv) == SUCCESS, "pmb chain read at 0 V\n");
    PMB_CHECK(failures, zero_mv >= 0 && zero_mv <= PMB_TEST_TOLERANCE_MV, "pmb chain idle voltage near 0\n");
    PMB_CHECK(failures, adg734_select_pixel(0) == SUCCESS, "pmb chain select pixel 0\n");
    PMB_CHECK(failures, pvd_dac_set_pixel_mv(PVD_PIXEL_FULL_SCALE_MV) == SUCCESS && test_pmb_measure_mv(&full_mv) == SUCCESS,
              "pmb chain read at full scale\n");
    PMB_CHECK(failures, full_mv - zero_mv >= (int)PMB_TEST_CHAIN_MIN_SWING_MV, "pmb chain responds to DAC\n");
    test_log("pmb: chain 0 V -> %d mV, full scale -> %d mV\n", zero_mv, full_mv);
    pvd_dac_set_code(0);
    adg734_deselect_all();

    test_log("--- pmb sanity: %d check(s) failed ---\n", failures);
    return failures;
}

void test_pmb(void) {
    test_log("----- testing pmb (on hardware) -----\n");

    if (test_pmb_sanity() != 0) {
        test_log("pmb: sanity checks failed, skipping sweep\n");
        pvd_dac_set_code(0);
        adg734_deselect_all();
        return;
    }

    for (uint8_t pixel = 0; pixel < PMB_PIXEL_COUNT; pixel++) {
        test_pmb_sweep_pixel(pixel);
    }

    // Leave the circuit idle
    PVDX_ASSERT_MSG(pvd_dac_set_code(0) == SUCCESS, "pmb dac to 0\n");
    PVDX_ASSERT_MSG(adg734_deselect_all() == SUCCESS, "pmb deselect all\n");
}
#endif // UNITTEST
