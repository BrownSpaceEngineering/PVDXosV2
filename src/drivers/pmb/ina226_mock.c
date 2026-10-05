/**
 * ina226_mock.c
 *
 * In-memory model of the INA226 register file, used in place of I2C in UNITTEST builds.
 * Tests set the raw shunt and bus voltage registers; the current and power registers are
 * derived from them using the calibration register (INA226 pg. 15, eq. 3-4), so the driver's
 * calibration and unit conversions are exercised end to end.
 */

#include "ina226.h"
#if defined(UNITTEST)

    #include <stdlib.h>

    #define INA226_MOCK_NUM_REGS 10

// Registers 0x00-0x07 map to indices 0-7; Manufacturer ID (0xFE) and Die ID (0xFF) map to 8 and 9
    #define REG(reg) mock_regs[mock_reg_index(reg)]

static uint16_t mock_regs[INA226_MOCK_NUM_REGS];
static uint16_t mock_unused_reg; // Absorbs mock_set_reg/get_reg on addresses the INA226 doesn't have
static bool mock_i2c_fail = false;

static bool mock_reg_exists(uint8_t reg) {
    return reg <= INA226_ALERT_LIMIT || reg == INA226_MANUFACTURER_ID || reg == INA226_DIE_ID;
}

static int mock_reg_index(uint8_t reg) {
    return reg <= INA226_ALERT_LIMIT ? reg : reg - INA226_MANUFACTURER_ID + INA226_ALERT_LIMIT + 1;
}

static uint16_t *mock_reg_ptr(uint8_t reg) {
    return mock_reg_exists(reg) ? &REG(reg) : &mock_unused_reg;
}

// Recompute current and power as the device would after a conversion
static void mock_update_derived(void) {
    int32_t shunt = (int16_t)REG(INA226_SHUNT_VOLTAGE);
    int32_t current = (shunt * (int32_t)REG(INA226_CALIBRATION)) / 2048;
    if (current > INT16_MAX) {
        current = INT16_MAX;
    } else if (current < INT16_MIN) {
        current = INT16_MIN;
    }
    REG(INA226_CURRENT) = (uint16_t)(int16_t)current;

    uint32_t power = ((uint32_t)abs(current) * REG(INA226_BUS_VOLTAGE)) / 20000U;
    REG(INA226_POWER) = (uint16_t)(power > UINT16_MAX ? UINT16_MAX : power);

    REG(INA226_MASK_ENABLE) |= INA226_ME_CVRF;
}

void ina226_mock_reset(void) {
    for (int i = 0; i < INA226_MOCK_NUM_REGS; i++) {
        mock_regs[i] = 0;
    }
    REG(INA226_CONFIG) = INA226_CONFIG_POR;
    REG(INA226_MANUFACTURER_ID) = INA226_MFG_ID;
    REG(INA226_DIE_ID) = INA226_DIE_ID_VALUE;
    mock_i2c_fail = false;
}

void ina226_mock_set_shunt_raw(int16_t raw) {
    REG(INA226_SHUNT_VOLTAGE) = (uint16_t)raw;
    mock_update_derived();
}

void ina226_mock_set_bus_raw(uint16_t raw) {
    REG(INA226_BUS_VOLTAGE) = raw & 0x7FFFU; // Bus voltage register is 15 bits, always positive
    mock_update_derived();
}

void ina226_mock_set_reg(uint8_t reg, uint16_t value) {
    *mock_reg_ptr(reg) = value;
}

uint16_t ina226_mock_get_reg(uint8_t reg) {
    return *mock_reg_ptr(reg);
}

void ina226_mock_set_i2c_fail(bool fail) {
    mock_i2c_fail = fail;
}

status_t ina226_write_reg(uint8_t reg, uint16_t value) {
    if (mock_i2c_fail) {
        return ERROR_I2C_FAILED;
    }
    if (!mock_reg_exists(reg)) {
        return ERROR_BAD_TARGET;
    }

    switch (reg) {
        case INA226_CONFIG:
            if (value & INA226_CONFIG_RST) {
                bool fail = mock_i2c_fail;
                ina226_mock_reset();
                mock_i2c_fail = fail;
            } else {
                REG(INA226_CONFIG) = INA226_CONFIG_FIXED | (value & INA226_CONFIG_WRITABLE_MASK);
                REG(INA226_MASK_ENABLE) &= (uint16_t)~INA226_ME_CVRF; // Config write clears CVRF
            }
            break;
        case INA226_CALIBRATION:
            REG(INA226_CALIBRATION) = value & INA226_CALIBRATION_MASK;
            mock_update_derived();
            break;
        case INA226_MASK_ENABLE:
            // Upper alert-enable/config bits are writable; low flag bits are read-only
            REG(INA226_MASK_ENABLE) = (value & 0xFC03U) | (REG(INA226_MASK_ENABLE) & 0x001CU);
            break;
        case INA226_ALERT_LIMIT:
            REG(INA226_ALERT_LIMIT) = value;
            break;
        default:
            break; // Read-only registers ignore writes
    }
    return SUCCESS;
}

status_t ina226_read_reg(uint8_t reg, uint16_t *value) {
    if (mock_i2c_fail) {
        return ERROR_I2C_FAILED;
    }
    if (!mock_reg_exists(reg)) {
        return ERROR_BAD_TARGET;
    }

    *value = REG(reg);
    if (reg == INA226_MASK_ENABLE) {
        REG(INA226_MASK_ENABLE) &= (uint16_t)~INA226_ME_CVRF; // Reading Mask/Enable clears CVRF
    }
    return SUCCESS;
}

#endif // UNITTEST
