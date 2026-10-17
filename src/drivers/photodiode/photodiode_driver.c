/**
 * photodiode_driver.c
 *
 * Hardware driver for photodiode sensors used in ADCS sun sensing.
 *
 * Created: September 20, 2025
 * Authors: Avinash Patel, Yi Lyo
 */
#include <inttypes.h>
#include "photodiode_driver.h"

// Photodiode system constants
#define PHOTODIODE_COUNT 22   // Number of photodiodes (8 mux + 14 direct)

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
 * \fn photodiode_read
 *
 * \brief Reads photodiode values and calculates sun vector
 *
 * \param data pointer to photodiode_data_t structure to fill
 *
 * \returns status_t SUCCESS if reading was successful
 */
status_t photodiode_read(photodiode_data_t *const data) {
    if (!data) {
        return ERROR_SANITY_CHECK_FAILED;
    }

    debug("photodiode: Reading photodiode values\n");

    // Read raw ADC values
    uint16_t raw_values[PHOTODIODE_COUNT];
    status_t result = read_photodiodes(raw_values);

    if (result != SUCCESS) {
        warning("photodiode: ADC read failed\n");
        return result;
    }

    // Copy raw values to data structure
    for (int i = 0; i < PHOTODIODE_COUNT; i++) {
        data->raw_values[i] = raw_values[i];
    }

    data->timestamp = xTaskGetTickCount();
    data->valid = true;

    return SUCCESS;
}