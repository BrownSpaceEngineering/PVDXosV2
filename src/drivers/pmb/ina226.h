#ifndef INA226_H
#define INA226_H

#include <stdbool.h>
#include <stdint.h>

#include "globals.h"
#include "ina226_regs.h"
#include "logging.h"

#define INA226_I2C_ADDR 0x40U /* 0b1000000, A1 = A0 = GND */

/*
 * PMB wiring (perovskites.kicad_sch): the TLV9001 follower output FOLLOW feeds the selected pixel through the
 * 10 ohm shunt RSH1 to PIXEL+, the node shared by every ADG734 SxA. IN- = FOLLOW, IN+ = PIXEL+ and VBUS = PIXEL+.
 *   - Bus voltage is the pixel voltage (PIXEL+), less only the switch on-resistance drop.
 *   - Shunt voltage = V(PIXEL+) - V(FOLLOW), so current is positive when the pixel sources current into the
 *     opamp (an illuminated cell) and negative when it draws current from the opamp (a dark or resistive load).
 *   - The opamp regulates FOLLOW, not PIXEL+: V(FOLLOW) = bus - shunt is the voltage the DAC commands.
 */

/*
 * Calibration (INA226 pg. 15, eq. 1-4)
 *   Max current   = 81.92 mV / 10 ohm          = 8.192 mA
 *   Current_LSB   = 8.192 mA / 2^15            = 250 nA
 *   CAL           = 0.00512 / (Current_LSB * R) = 2048
 *   Power_LSB     = 25 * Current_LSB            = 6.25 uW
 */
#define INA226_SHUNT_MOHMS 10000U /* RSH1, 10 ohm */
#define INA226_CURRENT_LSB_NA 250
#define INA226_POWER_LSB_NW (INA226_POWER_LSB_RATIO * INA226_CURRENT_LSB_NA)
#define INA226_CAL_VALUE ((uint16_t)(5120000000ULL / ((uint64_t)INA226_CURRENT_LSB_NA * INA226_SHUNT_MOHMS)))

/* 16-sample averaging, 1.1 ms conversions, continuous shunt + bus => new result every ~35 ms */
#define INA226_DEFAULT_CONFIG INA226_CONFIG_VALUE(INA226_AVG_16, INA226_CT_1100US, INA226_CT_1100US, INA226_MODE_SHUNT_BUS_CONTINUOUS)
#define INA226_CONVERSION_PERIOD_MS 36U

typedef struct {
    int32_t shunt_nv;   // Shunt voltage, V(PIXEL+) - V(FOLLOW) (nV)
    uint32_t bus_uv;    // Pixel voltage, V(PIXEL+) (uV)
    int32_t follow_uv;  // Opamp output, V(FOLLOW) = bus - shunt (uV); tracks the DAC command
    int32_t current_na; // Pixel current, positive when the pixel sources current (nA)
    uint32_t power_nw;  // Power magnitude from the device's power register (nW)
} ina226_measurement_t;

// Register access on the PMB I2C bus (see pmb_i2c.h)
status_t ina226_write_reg(uint8_t reg, uint16_t value);
status_t ina226_read_reg(uint8_t reg, uint16_t *value);

// Driver API
status_t ina226_init(void);
status_t ina226_reset(void);
status_t ina226_configure(uint16_t config);
status_t ina226_conversion_ready(bool *ready);
status_t ina226_wait_for_fresh_conversion(uint32_t timeout_ms);
status_t ina226_read_shunt_voltage_nv(int32_t *shunt_nv);
status_t ina226_read_bus_voltage_uv(uint32_t *bus_uv);
status_t ina226_read_current_na(int32_t *current_na);
status_t ina226_read_power_nw(uint32_t *power_nw);
status_t ina226_read_measurement(ina226_measurement_t *measurement);

#endif // INA226_H
