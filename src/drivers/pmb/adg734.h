#ifndef ADG734_H
#define ADG734_H

#include <stdint.h>

#include "globals.h"
#include "logging.h"

/*
 * Pixel routing through four ADG734 quad SPDT switches (ADG733/ADG734 datasheet Rev. B).
 * Each channel's IN pin is driven by one MCP23017 output. Per the truth table (Table II), IN = 1 connects
 * SxA-Dx and IN = 0 connects SxB-Dx. On the PMB (perovskites.kicad_sch), Dx is the pixel's positive terminal,
 * every SxA is the shared measurement node PIXEL+, and each SxB goes to ground through its own 100 ohm resistor.
 *
 * Chips U6-U9 are indices 0-3. Firmware pixel N (0-15) is schematic net PIXEL<N+1>+.
 *
 * The IN lines have no pull-downs, so the switches are undefined while the MCP23017 pins are inputs (power-on,
 * and after mcp23017_init() alone). Call adg734_init() before anything else on the MCP23017.
 */
#define ADG734_COUNT 4U
#define ADG734_CHANNELS 4U
#define PMB_PIXEL_COUNT (ADG734_COUNT * ADG734_CHANNELS)
#define PMB_PIXEL_NONE 0xFFU

#define ADG734_IN_MEASURE 1U // IN level that connects Dx to SxA (measurement)

typedef struct {
    uint8_t chip;    // ADG734 index, 0-3
    uint8_t channel; // Switch on that chip, 0-3 (IN1-IN4)
    uint8_t mcp_pin; // MCP23017 pin driving that IN (0-7 = GPA0-7, 8-15 = GPB0-7)
} adg734_route_t;

extern const adg734_route_t adg734_routes[PMB_PIXEL_COUNT];

status_t adg734_init(void);
status_t adg734_deselect_all(void);
status_t adg734_select_pixel(uint8_t pixel);
status_t adg734_read_selected(uint8_t *pixel);

#endif // ADG734_H
