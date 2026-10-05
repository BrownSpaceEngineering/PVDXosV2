/**
 * test_ina226.c
 *
 * Tests for the INA226 driver against the in-memory register model in drivers/pmb/ina226_mock.c.
 */

#include "tests/test_ina226.h"

#include "drivers/pmb/ina226.h"
#include "logging.h"
#include "tests/test.h"

#if defined(UNITTEST)
void test_ina226(void) {
    test_log("----- testing ina226 -----\n");
    ina226_mock_reset();

    // Calibration constant for a 10 ohm shunt with a 250 nA current LSB
    PVDX_ASSERT_MSG(INA226_CAL_VALUE == 2048, "ina226 cal constant\n");

    // init: verifies ID, writes config and calibration
    PVDX_ASSERT_MSG(ina226_init() == SUCCESS, "ina226 init\n");
    PVDX_ASSERT_MSG(ina226_mock_get_reg(INA226_CONFIG) == INA226_DEFAULT_CONFIG, "ina226 config written\n");
    PVDX_ASSERT_MSG(ina226_mock_get_reg(INA226_CALIBRATION) == INA226_CAL_VALUE, "ina226 calibration written\n");

    // Positive reading: 1.0 V across pixel, 5 mA through the 10 ohm shunt (50 mV)
    ina226_mock_set_bus_raw(800);     // 800 * 1.25 mV = 1.0 V
    ina226_mock_set_shunt_raw(20000); // 20000 * 2.5 uV = 50 mV
    ina226_measurement_t m = {0};
    PVDX_ASSERT_MSG(ina226_read_measurement(&m) == SUCCESS, "ina226 read measurement\n");
    test_log("shunt %ld nV, bus %lu uV, current %ld nA, power %lu nW\n", (long)m.shunt_nv, (unsigned long)m.bus_uv, (long)m.current_na,
             (unsigned long)m.power_nw);
    PVDX_ASSERT_MSG(m.shunt_nv == 50000000, "ina226 shunt voltage\n");
    PVDX_ASSERT_MSG(m.bus_uv == 1000000, "ina226 bus voltage\n");
    PVDX_ASSERT_MSG(m.current_na == 5000000, "ina226 current\n");
    PVDX_ASSERT_MSG(m.power_nw == 5000000, "ina226 power\n");

    // Negative current (pixel sourcing current back into the opamp)
    ina226_mock_set_shunt_raw(-4000); // -10 mV => -1 mA
    int32_t current_na = 0;
    PVDX_ASSERT_MSG(ina226_read_current_na(&current_na) == SUCCESS && current_na == -1000000, "ina226 negative current\n");
    int32_t shunt_nv = 0;
    PVDX_ASSERT_MSG(ina226_read_shunt_voltage_nv(&shunt_nv) == SUCCESS && shunt_nv == -10000000, "ina226 negative shunt\n");

    // Conversion ready flag is set by a conversion and cleared by reading Mask/Enable
    bool ready = false;
    PVDX_ASSERT_MSG(ina226_conversion_ready(&ready) == SUCCESS && ready, "ina226 conversion ready set\n");
    PVDX_ASSERT_MSG(ina226_conversion_ready(&ready) == SUCCESS && !ready, "ina226 conversion ready cleared\n");

    // Reset returns registers to power-on values
    PVDX_ASSERT_MSG(ina226_reset() == SUCCESS, "ina226 reset\n");
    PVDX_ASSERT_MSG(ina226_mock_get_reg(INA226_CONFIG) == INA226_CONFIG_POR, "ina226 reset config\n");
    PVDX_ASSERT_MSG(ina226_mock_get_reg(INA226_CALIBRATION) == 0, "ina226 reset calibration\n");

    // Wrong manufacturer ID is rejected
    test_log("expect warning: unexpected manufacturer ID\n");
    ina226_mock_set_reg(INA226_MANUFACTURER_ID, 0x1234);
    PVDX_ASSERT_MSG(ina226_init() == ERROR_SANITY_CHECK_FAILED, "ina226 bad manufacturer ID\n");
    ina226_mock_reset();

    // I2C failures propagate
    test_log("expect warning: failed to read manufacturer ID\n");
    ina226_mock_set_i2c_fail(true);
    PVDX_ASSERT_MSG(ina226_init() == ERROR_I2C_FAILED, "ina226 init i2c failure\n");
    PVDX_ASSERT_MSG(ina226_read_measurement(&m) == ERROR_I2C_FAILED, "ina226 read i2c failure\n");
    ina226_mock_set_i2c_fail(false);
}
#endif // UNITTEST
