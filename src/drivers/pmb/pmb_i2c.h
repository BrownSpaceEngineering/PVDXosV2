#ifndef PMB_I2C_H
#define PMB_I2C_H

#include <stdint.h>

#include "driver_init.h"
#include "globals.h"

// The PMB I2C bus is SERCOM6 (SDA PD09 / SCL PD08), shared by the INA226 and MCP23017.
// Atmel START labels it I2C_CAMERA; the generated name is kept. The PMB has no pull-ups on SDA/SCL; they are on
// the flight computer board.
#define PMB_I2C I2C_CAMERA

#define PMB_I2C_MAX_WRITE_LEN 2U // Largest register payload on the bus (INA226 16-bit registers)

status_t pmb_i2c_init(void);
status_t pmb_i2c_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, uint8_t len);
status_t pmb_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, uint8_t len);
status_t pmb_i2c_probe(uint8_t addr);

#endif // PMB_I2C_H
