/**
 * mcp23017.c
 *
 * Generic driver for the MCP23017 16-bit I2C I/O expander on the PMB bus (MCP23017 datasheet DS20001952D).
 */

#include "mcp23017.h"

#include "pmb_i2c.h"

status_t mcp23017_write_reg(uint8_t reg, uint8_t value) {
    return pmb_i2c_write_reg(MCP23017_I2C_ADDR, reg, &value, 1);
}

status_t mcp23017_read_reg(uint8_t reg, uint8_t *value) {
    return pmb_i2c_read_reg(MCP23017_I2C_ADDR, reg, value, 1);
}

/**
 * Writes a port A register and its port B pair in one transfer (BANK = 0 places them at reg_a and reg_a + 1,
 * and sequential mode increments the address pointer).
 */
status_t mcp23017_write_reg16(uint8_t reg_a, uint16_t value) {
    uint8_t buf[2] = {(uint8_t)(value & 0xFF), (uint8_t)(value >> 8)};
    return pmb_i2c_write_reg(MCP23017_I2C_ADDR, reg_a, buf, sizeof(buf));
}

status_t mcp23017_read_reg16(uint8_t reg_a, uint16_t *value) {
    uint8_t buf[2] = {0};
    status_t status = pmb_i2c_read_reg(MCP23017_I2C_ADDR, reg_a, buf, sizeof(buf));
    if (status != SUCCESS) {
        return status;
    }
    *value = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    return SUCCESS;
}

/**
 * Selects BANK = 0 with sequential addressing, which the 16-bit accessors rely on, and confirms the device
 * responds. GPA7/GPB7 are made outputs as the datasheet requires (driving their power-on latch value, low);
 * all other pins are left as power-on inputs.
 */
status_t mcp23017_init(void) {
    status_t status = pmb_i2c_init();
    if (status != SUCCESS) {
        return status;
    }
    if ((status = mcp23017_write_reg(MCP23017_IOCON, 0x00)) != SUCCESS) {
        warning("mcp23017: no response at 0x%02x\n", MCP23017_I2C_ADDR);
        return status;
    }
    uint8_t iocon = 0xFF;
    if ((status = mcp23017_read_reg(MCP23017_IOCON, &iocon)) != SUCCESS) {
        return status;
    }
    if (iocon != 0x00) {
        warning("mcp23017: IOCON readback 0x%02x (expected 0x00)\n", iocon);
        return ERROR_SANITY_CHECK_FAILED;
    }
    uint16_t iodir = 0;
    if ((status = mcp23017_read_reg16(MCP23017_IODIRA, &iodir)) != SUCCESS) {
        return status;
    }
    return mcp23017_set_direction(iodir & MCP23017_MAX_INPUTS);
}

/**
 * Sets every pin's direction; bit N = 1 makes pin N an input. Set output levels with mcp23017_write_outputs()
 * first so pins don't glitch when they become outputs. GPA7/GPB7 cannot be inputs.
 */
status_t mcp23017_set_direction(uint16_t iodir) {
    if (iodir & MCP23017_OUTPUT_ONLY_PINS) {
        warning("mcp23017: GPA7/GPB7 are output-only (requested IODIR 0x%04x)\n", iodir);
        return ERROR_BAD_TARGET;
    }
    return mcp23017_write_reg16(MCP23017_IODIRA, iodir);
}

// Enables the 100 kOhm pull-up on each input pin whose bit is set
status_t mcp23017_set_pullups(uint16_t mask) {
    return mcp23017_write_reg16(MCP23017_GPPUA, mask);
}

// Inverts the value read from each input pin whose bit is set
status_t mcp23017_set_input_polarity(uint16_t mask) {
    return mcp23017_write_reg16(MCP23017_IPOLA, mask);
}

// Writes all 16 output latches; only pins configured as outputs drive the value
status_t mcp23017_write_outputs(uint16_t levels) {
    return mcp23017_write_reg16(MCP23017_OLATA, levels);
}

status_t mcp23017_read_output_latches(uint16_t *levels) {
    return mcp23017_read_reg16(MCP23017_OLATA, levels);
}

// Reads the actual level on every pin, inputs and outputs alike
status_t mcp23017_read_pins(uint16_t *levels) {
    return mcp23017_read_reg16(MCP23017_GPIOA, levels);
}

/**
 * Sets one output latch, preserving the others (read-modify-write of that port's OLAT).
 */
status_t mcp23017_write_pin(uint8_t pin, bool level) {
    if (pin >= MCP23017_PIN_COUNT) {
        return ERROR_BAD_TARGET;
    }
    uint8_t reg = (pin < 8) ? MCP23017_OLATA : MCP23017_OLATB;
    uint8_t bit = (uint8_t)(1U << (pin % 8));
    uint8_t olat = 0;
    status_t status = mcp23017_read_reg(reg, &olat);
    if (status != SUCCESS) {
        return status;
    }
    olat = level ? (uint8_t)(olat | bit) : (uint8_t)(olat & ~bit);
    return mcp23017_write_reg(reg, olat);
}

status_t mcp23017_read_pin(uint8_t pin, bool *level) {
    if (pin >= MCP23017_PIN_COUNT) {
        return ERROR_BAD_TARGET;
    }
    uint8_t gpio = 0;
    status_t status = mcp23017_read_reg((pin < 8) ? MCP23017_GPIOA : MCP23017_GPIOB, &gpio);
    if (status != SUCCESS) {
        return status;
    }
    *level = (gpio >> (pin % 8)) & 1U;
    return SUCCESS;
}
