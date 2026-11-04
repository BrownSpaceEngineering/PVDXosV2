/**
 * pmb_i2c.c
 *
 * Register access on the PMB I2C bus. Both devices use the same framing:
 * write = [reg, data...], read = [reg] then repeated start and read data.
 */

#include "pmb_i2c.h"

#include <stdbool.h>
#include <string.h>

#include "hal_i2c_m_sync.h"
#include "logging.h"
#include "mutexes.h"

static bool pmb_i2c_enabled = false;

/**
 * Enables the SERCOM; system_init() initializes it but leaves it disabled. Safe to call more than once.
 */
status_t pmb_i2c_init(void) {
    lock_mutex(i2c_camera_bus_mutex);
    if (pmb_i2c_enabled) {
        unlock_mutex(i2c_camera_bus_mutex);
        return SUCCESS;
    }
    if (i2c_m_sync_enable(&PMB_I2C) != 0) {
        unlock_mutex(i2c_camera_bus_mutex);
        warning("pmb_i2c: failed to enable bus\n");
        return ERROR_I2C_FAILED;
    }
    pmb_i2c_enabled = true;
    unlock_mutex(i2c_camera_bus_mutex);
    return SUCCESS;
}

status_t pmb_i2c_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, uint8_t len) {
    if (len > PMB_I2C_MAX_WRITE_LEN) {
        return ERROR_WRITE_FAILED;
    }
    uint8_t buf[1 + PMB_I2C_MAX_WRITE_LEN];
    buf[0] = reg;
    memcpy(&buf[1], data, len);

    struct _i2c_m_msg msg = {
        .addr = addr,
        .len = 1 + len,
        .buffer = buf,
        .flags = I2C_M_SEVEN | I2C_M_STOP,
    };
    lock_mutex(i2c_camera_bus_mutex);
    int32_t rc = _i2c_m_sync_transfer(&PMB_I2C.device, &msg);
    unlock_mutex(i2c_camera_bus_mutex);
    if (rc != 0) {
        return ERROR_I2C_FAILED;
    }
    return SUCCESS;
}

status_t pmb_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, uint8_t len) {
    struct _i2c_m_msg wr = {
        .addr = addr,
        .len = 1,
        .buffer = &reg,
        .flags = I2C_M_SEVEN,
    };
    struct _i2c_m_msg rd = {
        .addr = addr,
        .len = len,
        .buffer = data,
        .flags = I2C_M_SEVEN | I2C_M_RD | I2C_M_STOP,
    };
    lock_mutex(i2c_camera_bus_mutex);
    if (_i2c_m_sync_transfer(&PMB_I2C.device, &wr) != 0) {
        unlock_mutex(i2c_camera_bus_mutex);
        return ERROR_I2C_FAILED;
    }
    int32_t rc = _i2c_m_sync_transfer(&PMB_I2C.device, &rd);
    unlock_mutex(i2c_camera_bus_mutex);
    if (rc != 0) {
        return ERROR_I2C_FAILED;
    }
    return SUCCESS;
}

/**
 * Checks that a device acknowledges its address, using a 1-byte read that doesn't touch any register pointer.
 */
status_t pmb_i2c_probe(uint8_t addr) {
    uint8_t byte = 0;
    struct _i2c_m_msg rd = {
        .addr = addr,
        .len = 1,
        .buffer = &byte,
        .flags = I2C_M_SEVEN | I2C_M_RD | I2C_M_STOP,
    };
    lock_mutex(i2c_camera_bus_mutex);
    int32_t rc = _i2c_m_sync_transfer(&PMB_I2C.device, &rd);
    unlock_mutex(i2c_camera_bus_mutex);
    if (rc != 0) {
        return ERROR_I2C_FAILED;
    }
    return SUCCESS;
}
