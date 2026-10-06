/**
 * adg734.c
 *
 * Connects one pixel at a time to the PMB measurement circuit by driving the ADG734 IN pins through the
 * MCP23017. Unselected pixels sit on SxB, tied to ground through a resistor.
 */

#include "adg734.h"

#include "mcp23017.h"

// Board routing: ADG734 #k channel c is pixel 4k + c, and pixel N's IN pin is MCP23017 pin N
const adg734_route_t adg734_routes[PMB_PIXEL_COUNT] = {
    {0, 0, 0},  {0, 1, 1},  {0, 2, 2},  {0, 3, 3},  //
    {1, 0, 4},  {1, 1, 5},  {1, 2, 6},  {1, 3, 7},  //
    {2, 0, 8},  {2, 1, 9},  {2, 2, 10}, {2, 3, 11}, //
    {3, 0, 12}, {3, 1, 13}, {3, 2, 14}, {3, 3, 15}, //
};

// MCP23017 pins that drive an ADG734 IN
static uint16_t adg734_in_pin_mask(void) {
    uint16_t mask = 0;
    for (uint8_t pixel = 0; pixel < PMB_PIXEL_COUNT; pixel++) {
        mask |= (uint16_t)(1U << adg734_routes[pixel].mcp_pin);
    }
    return mask;
}

// MCP23017 output levels that route `pixel` to measurement and every other pixel to ground
static uint16_t adg734_levels_for(uint8_t pixel) {
    uint16_t levels = 0;
    for (uint8_t p = 0; p < PMB_PIXEL_COUNT; p++) {
        uint8_t in = (p == pixel) ? ADG734_IN_MEASURE : !ADG734_IN_MEASURE;
        if (in) {
            levels |= (uint16_t)(1U << adg734_routes[p].mcp_pin);
        }
    }
    return levels;
}

/**
 * Brings up the MCP23017 and drives every IN pin so all pixels are grounded. Latches are written before the
 * pins become outputs so no switch moves to the measurement side during init.
 */
status_t adg734_init(void) {
    status_t status = mcp23017_init();
    if (status != SUCCESS) {
        return status;
    }
    if ((status = mcp23017_write_outputs(adg734_levels_for(PMB_PIXEL_NONE))) != SUCCESS) {
        return status;
    }
    if ((status = mcp23017_set_direction((uint16_t)~adg734_in_pin_mask())) != SUCCESS) {
        return status;
    }

    uint8_t selected = 0;
    if ((status = adg734_read_selected(&selected)) != SUCCESS) {
        return status;
    }
    if (selected != PMB_PIXEL_NONE) {
        warning("adg734: pixel %u still selected after init\n", selected);
        return ERROR_SANITY_CHECK_FAILED;
    }
    debug("adg734: initialized, all pixels grounded\n");
    return SUCCESS;
}

status_t adg734_deselect_all(void) {
    return mcp23017_write_outputs(adg734_levels_for(PMB_PIXEL_NONE));
}

/**
 * Connects one pixel to the measurement circuit. Each ADG734 switch is break-before-make on its own, but the
 * MCP23017 updates port A and port B one after the other, so switching directly between pixels on different
 * ports could briefly connect two. All pixels are grounded first.
 */
status_t adg734_select_pixel(uint8_t pixel) {
    if (pixel >= PMB_PIXEL_COUNT) {
        return ERROR_BAD_TARGET;
    }
    status_t status = adg734_deselect_all();
    if (status != SUCCESS) {
        return status;
    }
    return mcp23017_write_outputs(adg734_levels_for(pixel));
}

/**
 * Reads the IN pin levels back from the MCP23017 and reports which pixel is on the measurement side,
 * PMB_PIXEL_NONE if none, or ERROR_SANITY_CHECK_FAILED if more than one is.
 */
status_t adg734_read_selected(uint8_t *pixel) {
    uint16_t levels = 0;
    status_t status = mcp23017_read_pins(&levels);
    if (status != SUCCESS) {
        return status;
    }
    *pixel = PMB_PIXEL_NONE;
    for (uint8_t p = 0; p < PMB_PIXEL_COUNT; p++) {
        uint8_t in = (levels >> adg734_routes[p].mcp_pin) & 1U;
        if (in == ADG734_IN_MEASURE) {
            if (*pixel != PMB_PIXEL_NONE) {
                warning("adg734: pixels %u and %u both selected\n", *pixel, p);
                return ERROR_SANITY_CHECK_FAILED;
            }
            *pixel = p;
        }
    }
    return SUCCESS;
}
