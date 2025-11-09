/**
 * photodiode_driver.c
 *
 * Hardware driver for photodiode sensors used in ADCS sun sensing.
 *
 * Created: September 20, 2025
 * Modified: November 9, 2025
 * Authors: Avinash Patel, Yi Lyo, Alexander Thaep
 */
#include <inttypes.h>
#include "photodiode_driver.h"
#include "adcs_task.h"

/**
 * \fn init_photodiode_hardware
 *
 * \brief Initialize photodiode hardware (ADC channels)
 *
 * \returns status_t SUCCESS if initialization was successful
 */
status_t init_photodiode_hardware(void) {
    debug("photodiode_driver: Initializing photodiode hardware\n");

    // TODO: Configure ADC channels for photodiode readings
    // TODO: Initialize any required peripherals

    info("photodiode_driver: Hardware initialization complete\n");
    return SUCCESS;
}

/**
 * \fn read_photodiodes
 *
 * \brief Read ADC values from photodiode sensors
 *
 * \param values pointer to array to store ADC readings
 * \param count number of photodiodes to read
 *
 * \returns status_t SUCCESS if reading was successful
 */
status_t read_photodiodes(uint16_t *values) {
    if (!values) {
        return ERROR_SANITY_CHECK_FAILED;
    }

    debug("photodiode_driver: Reading photodiode ADC values\n");

    // TODO: Read values from ADCs

    return SUCCESS;
}

/**
 * \fn set_multiplexer_outputs
 *
 * \brief Set multiplexer outputs to enable/disable and select channel
 *
 * \param output Output to write. The three least significant bits determine
 *      channel, the fourth bit determines enable pin. Note the enable pin is
 *      active low. Thus, if output is between 0 and 7 inclusive, then the
 *      multiplexer is enabled and the appropriate channel is selected. If
 *      output is between 8 and 15 inclusive, then the multiplexer is disabled.
 *      Values outside this range are not permitted and will result in
 *      ERROR_SANITY_CHECK_FAILED being returned.
 *
 * \returns status_t SUCCESS if output write was successful,
 *                   ERROR_SANITY_CHECK_FAILED if output is invalid
 */
status_t set_multiplexer_outputs(int_fast8_t output) {
    if (output & (~0xF)) { // Only the four least significant bits should be set
        return ERROR_SANITY_CHECK_FAILED;
    }
    static int_fast8_t current_output = 0xF;

    debug("photodiode_driver: Setting multiplexer output to " PRIxFAST8 "\n", output);

    int_fast8_t toggles_needed = output ^ current_output;
    PORT->Group[GPIO_PORTC].OUTTGL.reg = toggles_needed << PHOTODIODE_S0_PIN;
    current_output = output;

    debug("photodiode_driver: MUX output set to " PRIxFAST8 "\n", output);

    return SUCCESS;
}
