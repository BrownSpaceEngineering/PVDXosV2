#include "ina226.h"

#include <FreeRTOS.h>
#include <task.h>

#include "checks/device_checks.h"
#include "os_delay.h"
#include "pmb_i2c.h"

status_t ina226_write_reg(uint8_t reg, uint16_t value) {
    uint8_t buf[2] = {(uint8_t)(value >> 8), (uint8_t)(value & 0xFF)}; /* INA226 is MSB first */
    return pmb_i2c_write_reg(INA226_I2C_ADDR, reg, buf, sizeof(buf));
}

status_t ina226_read_reg(uint8_t reg, uint16_t *value) {
    uint8_t buf[2] = {0};
    status_t status = pmb_i2c_read_reg(INA226_I2C_ADDR, reg, buf, sizeof(buf));
    if (status != SUCCESS) {
        return status;
    }
    *value = ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
    return SUCCESS;
}

/**
 * Verifies the device ID, resets it, then writes the default configuration and calibration.
 */
status_t ina226_init(void) {
    // A disabled board's ID reads 0, which would fail the check below
    if (device_disabled(PEROVSKITE_ID)) {
        return SUCCESS;
    }
    uint16_t mfg_id = 0;
    status_t status = ina226_read_reg(INA226_MANUFACTURER_ID, &mfg_id);
    if (status != SUCCESS) {
        warning("ina226: failed to read manufacturer ID\n");
        return status;
    }
    if (mfg_id != INA226_MFG_ID) {
        warning("ina226: unexpected manufacturer ID 0x%04x (expected 0x%04x)\n", mfg_id, INA226_MFG_ID);
        return ERROR_SANITY_CHECK_FAILED;
    }

    if ((status = ina226_reset()) != SUCCESS) {
        return status;
    }
    if ((status = ina226_configure(INA226_DEFAULT_CONFIG)) != SUCCESS) {
        return status;
    }
    if ((status = ina226_write_reg(INA226_CALIBRATION, INA226_CAL_VALUE)) != SUCCESS) {
        return status;
    }

    // Calibration must be nonzero for the current/power registers to be valid
    uint16_t cal = 0;
    if ((status = ina226_read_reg(INA226_CALIBRATION, &cal)) != SUCCESS) {
        return status;
    }
    if (cal != INA226_CAL_VALUE) {
        warning("ina226: calibration readback 0x%04x (expected 0x%04x)\n", cal, INA226_CAL_VALUE);
        return ERROR_SANITY_CHECK_FAILED;
    }

    debug("ina226: initialized (config 0x%04x, cal %u)\n", INA226_DEFAULT_CONFIG, INA226_CAL_VALUE);
    return SUCCESS;
}

/**
 * Software reset: all registers return to their power-on values (calibration becomes 0).
 */
status_t ina226_reset(void) {
    return ina226_write_reg(INA226_CONFIG, INA226_CONFIG_POR | INA226_CONFIG_RST);
}

status_t ina226_configure(uint16_t config) {
    return ina226_write_reg(INA226_CONFIG, (uint16_t)(config & ~INA226_CONFIG_RST));
}

/**
 * Note: reading Mask/Enable clears the conversion ready flag.
 */
status_t ina226_conversion_ready(bool *ready) {
    // A disabled board's flag reads 0, so report ready rather than poll until the timeout
    if (device_disabled(PEROVSKITE_ID)) {
        *ready = true;
        return SUCCESS;
    }
    uint16_t me = 0;
    status_t status = ina226_read_reg(INA226_MASK_ENABLE, &me);
    if (status != SUCCESS) {
        return status;
    }
    *ready = (me & INA226_ME_CVRF) != 0;
    return SUCCESS;
}

/**
 * Whether timeout_ms has passed since start_ticks. Before the scheduler starts the tick count stands still, so each
 * poll is counted as 1 ms instead, which only lengthens the wait (each poll is a 1 ms delay plus an I2C read).
 */
static bool ina226_timed_out(TickType_t start_ticks, uint32_t *polls, uint32_t timeout_ms) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return (*polls)++ >= timeout_ms;
    }
    return (xTaskGetTickCount() - start_ticks) >= pdMS_TO_TICKS(timeout_ms);
}

/**
 * Waits until a conversion that started after this call has completed, so the next read reflects the
 * current input. The conversion in flight when called may include samples taken before an input change,
 * so it is discarded and the following one is waited for. Polls about every 1 ms, and gives up timeout_ms after
 * the call.
 */
status_t ina226_wait_for_fresh_conversion(uint32_t timeout_ms) {
    const TickType_t start_ticks = xTaskGetTickCount();
    uint32_t polls = 0;
    bool ready = false;
    status_t status = ina226_conversion_ready(&ready); // Clears any stale flag
    if (status != SUCCESS) {
        return status;
    }
    for (int completed = 0; completed < 2;) {
        if (ina226_timed_out(start_ticks, &polls, timeout_ms)) {
            return ERROR_NOT_READY;
        }
        os_delay_ms(1);
        if ((status = ina226_conversion_ready(&ready)) != SUCCESS) {
            return status;
        }
        if (ready) {
            completed++;
        }
    }
    return SUCCESS;
}

status_t ina226_read_shunt_voltage_nv(int32_t *shunt_nv) {
    uint16_t raw = 0;
    status_t status = ina226_read_reg(INA226_SHUNT_VOLTAGE, &raw);
    if (status != SUCCESS) {
        return status;
    }
    *shunt_nv = (int32_t)(int16_t)raw * INA226_SHUNT_LSB_NV;
    return SUCCESS;
}

status_t ina226_read_bus_voltage_uv(uint32_t *bus_uv) {
    uint16_t raw = 0;
    status_t status = ina226_read_reg(INA226_BUS_VOLTAGE, &raw);
    if (status != SUCCESS) {
        return status;
    }
    *bus_uv = (uint32_t)raw * INA226_BUS_LSB_UV;
    return SUCCESS;
}

status_t ina226_read_current_na(int32_t *current_na) {
    uint16_t raw = 0;
    status_t status = ina226_read_reg(INA226_CURRENT, &raw);
    if (status != SUCCESS) {
        return status;
    }
    *current_na = (int32_t)(int16_t)raw * INA226_CURRENT_LSB_NA;
    return SUCCESS;
}

status_t ina226_read_power_nw(uint32_t *power_nw) {
    uint16_t raw = 0;
    status_t status = ina226_read_reg(INA226_POWER, &raw);
    if (status != SUCCESS) {
        return status;
    }
    *power_nw = (uint32_t)raw * INA226_POWER_LSB_NW;
    return SUCCESS;
}

status_t ina226_read_measurement(ina226_measurement_t *measurement) {
    status_t status;
    if ((status = ina226_read_shunt_voltage_nv(&measurement->shunt_nv)) != SUCCESS) {
        return status;
    }
    if ((status = ina226_read_bus_voltage_uv(&measurement->bus_uv)) != SUCCESS) {
        return status;
    }
    measurement->follow_uv = (int32_t)measurement->bus_uv - measurement->shunt_nv / 1000;
    if ((status = ina226_read_current_na(&measurement->current_na)) != SUCCESS) {
        return status;
    }
    return ina226_read_power_nw(&measurement->power_nw);
}
