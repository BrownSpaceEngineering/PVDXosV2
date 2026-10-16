#ifndef MCP23017_H
#define MCP23017_H

#include <stdbool.h>
#include <stdint.h>

#include "globals.h"
#include "logging.h"
#include "mcp23017_regs.h"

#define MCP23017_I2C_ADDR 0x20U /* 0b0100000, A2 = A1 = A0 = GND */

/*
 * Pins are numbered 0-15: GPA0-7 are pins 0-7 and GPB0-7 are pins 8-15. The 16-bit port functions use the
 * same layout, with port A in the low byte. Direction follows the chip's IODIR convention: 1 = input.
 */
#define MCP23017_PIN_COUNT 16U
#define MCP23017_ALL_OUTPUTS 0x0000U

// GPA7 and GPB7 are output-only on the MCP23017; their IODIR bits must be 0 (DS20001952D, sec. 3.5.1)
#define MCP23017_OUTPUT_ONLY_PINS 0x8080U
#define MCP23017_MAX_INPUTS ((uint16_t)~MCP23017_OUTPUT_ONLY_PINS)

status_t mcp23017_write_reg(uint8_t reg, uint8_t value);
status_t mcp23017_read_reg(uint8_t reg, uint8_t *value);
status_t mcp23017_write_reg16(uint8_t reg_a, uint16_t value);
status_t mcp23017_read_reg16(uint8_t reg_a, uint16_t *value);

status_t mcp23017_init(void);
status_t mcp23017_set_direction(uint16_t iodir);
status_t mcp23017_set_pullups(uint16_t mask);
status_t mcp23017_set_input_polarity(uint16_t mask);
status_t mcp23017_write_outputs(uint16_t levels);
status_t mcp23017_read_output_latches(uint16_t *levels);
status_t mcp23017_read_pins(uint16_t *levels);
status_t mcp23017_write_pin(uint8_t pin, bool level);
status_t mcp23017_read_pin(uint8_t pin, bool *level);

#endif // MCP23017_H
