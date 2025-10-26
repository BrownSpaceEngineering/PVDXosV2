/**
 * photodiode_driver.c
 *
 * Hardware driver for photodiode sensors used in ADCS sun sensing.
 *
 * Created: September 20, 2024
 * Authors: Avinash Patel
 */

#include "photodiode_driver.h"
#include "photodiode_task.h"

/**
 * \fn init_photodiode_hardware
 *
 * \brief Initialize photodiode hardware (ADC channels)
 *
 * \returns status_t SUCCESS if initialization was successful
 */
status_t init_photodiode_hardware(void) {
    debug("photodiode_driver: Initializing photodiode hardware\n");
    
    // Initialize multiplexer GPIO pins
    ret_err_status(init_multiplexer_gpio(), "photodiode_driver: Multiplexer GPIO initialization failed");
    
    // TODO: Configure ADC channels for photodiode readings
    // TODO: Initialize any required peripherals
    
    info("photodiode_driver: Hardware initialization complete\n");
    return SUCCESS;
}

/**
 * \fn read_photodiode_adc
 *
 * \brief Read ADC values from photodiode sensors
 *
 * \param values pointer to array to store ADC readings
 * \param count number of photodiodes to read
 *
 * \returns status_t SUCCESS if reading was successful
 */
status_t read_photodiode_adc(uint16_t *values, uint8_t count) {
    if (!values || count == 0 || count > PHOTODIODE_MAX_COUNT) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    debug("photodiode_driver: Reading %d photodiode ADC values\n", count);
    
    // Read each photodiode through the multiplexer
    for (uint8_t i = 0; i < count; i++) {
        ret_err_status(read_single_photodiode_adc(i, &values[i]), 
                       "photodiode_driver: Failed to read photodiode");
    }
    
    return SUCCESS;
}

/**
 * \fn calibrate_photodiode_readings
 *
 * \brief Calibrate raw ADC readings to light intensity values
 *
 * \param raw_values pointer to raw ADC readings
 * \param calibrated_values pointer to array for calibrated values
 * \param count number of values to calibrate
 *
 * \returns status_t SUCCESS if calibration was successful
 */
status_t calibrate_photodiode_readings(const uint16_t *raw_values, float *calibrated_values, uint8_t count) {
    if (!raw_values || !calibrated_values || count == 0) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    debug("photodiode_driver: Calibrating %d photodiode readings\n", count);
    
    // TODO: Implement actual calibration algorithm
    // For now, simple linear scaling
    for (uint8_t i = 0; i < count; i++) {
        calibrated_values[i] = (float)raw_values[i] / PHOTODIODE_MAX_ADC_VALUE;
    }
    
    return SUCCESS;
}

/**
 * \fn calculate_sun_vector
 *
 * \brief Calculate sun direction vector from photodiode readings
 *
 * \param calibrated_values pointer to calibrated photodiode values
 * \param sun_vector pointer to array for sun vector [x, y, z]
 *
 * \returns status_t SUCCESS if calculation was successful
 */
status_t calculate_sun_vector(float *calibrated_values, float *sun_vector) {
    if (!calibrated_values || !sun_vector) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    debug("photodiode_driver: Calculating sun vector\n");
    
    // TODO: Implement actual sun vector calculation algorithm
    // This is a simplified example - real implementation would use
    // proper sun sensing algorithms based on photodiode geometry
    
    // For now, return dummy sun vector
    sun_vector[0] = 0.0f;  // X component
    sun_vector[1] = 0.0f;  // Y component
    sun_vector[2] = 1.0f;  // Z component (pointing "up")
    
    return SUCCESS;
}

/**
 * \fn init_multiplexer_gpio
 *
 * \brief Initialize GPIO pins for multiplexer control
 *
 * \returns status_t SUCCESS if initialization was successful
 */
status_t init_multiplexer_gpio(void) {
    debug("photodiode_driver: Initializing multiplexer GPIO pins\n");
    
    // Configure GPIO pins for multiplexer select lines
    for (uint8_t i = 0; i < PHOTODIODE_MUX_SELECT_BITS; i++) {
        // Set pin direction to output
        gpio_set_pin_direction(photodiode_config.mux_select_pins[i], GPIO_DIRECTION_OUT);
        
        // Initialize pins to low (channel 0)
        gpio_set_pin_level(photodiode_config.mux_select_pins[i], false);
        
        // Set pin function to GPIO
        gpio_set_pin_function(photodiode_config.mux_select_pins[i], GPIO_PIN_FUNCTION_OFF);
        
        debug("photodiode_driver: Configured MUX select pin %d\n", i);
    }
    
    // Configure multiplexer enable pin
    gpio_set_pin_direction(photodiode_config.mux_enable_pin, GPIO_DIRECTION_OUT);
    gpio_set_pin_level(photodiode_config.mux_enable_pin, true); // Start with multiplexer disabled (active low)
    gpio_set_pin_function(photodiode_config.mux_enable_pin, GPIO_PIN_FUNCTION_OFF);
    
    debug("photodiode_driver: Configured MUX enable pin\n");
    
    info("photodiode_driver: Multiplexer GPIO initialization complete\n");
    return SUCCESS;
}

/**
 * \fn set_multiplexer_channel
 *
 * \brief Set multiplexer to select specified channel
 *
 * \param channel channel number to select (0-7 for multiplexer)
 *
 * \returns status_t SUCCESS if channel selection was successful
 */
status_t set_multiplexer_channel(uint8_t channel) {
    if (channel >= PHOTODIODE_MUX_CHANNELS) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    debug("photodiode_driver: Setting multiplexer to channel %d\n", channel);
    
    // Enable multiplexer first (active low)
    gpio_set_pin_level(photodiode_config.mux_enable_pin, false);
    
    // Set multiplexer select pins atomically based on channel number
    // All three pins are in the same register, so we can write them together
    gpio_set_pin_level(photodiode_config.mux_select_pins[0], (channel & 0x01) ? true : false);
    gpio_set_pin_level(photodiode_config.mux_select_pins[1], (channel & 0x02) ? true : false);
    gpio_set_pin_level(photodiode_config.mux_select_pins[2], (channel & 0x04) ? true : false);
    
    debug("photodiode_driver: MUX channel %d (binary: %d%d%d)\n", 
          channel, 
          (channel & 0x04) ? 1 : 0,
          (channel & 0x02) ? 1 : 0,
          (channel & 0x01) ? 1 : 0);
    
    // Wait for multiplexer to settle
    vTaskDelay(pdMS_TO_TICKS(PHOTODIODE_MUX_SETTLE_TIME_MS));
    
    return SUCCESS;
}

/**
 * \fn enable_multiplexer
 *
 * \brief Enable the multiplexer
 *
 * \returns status_t SUCCESS if enable was successful
 */
status_t enable_multiplexer(void) {
    debug("photodiode_driver: Enabling multiplexer\n");
    gpio_set_pin_level(photodiode_config.mux_enable_pin, false); // Active low - LOW enables
    return SUCCESS;
}

/**
 * \fn disable_multiplexer
 *
 * \brief Disable the multiplexer
 *
 * \returns status_t SUCCESS if disable was successful
 */
status_t disable_multiplexer(void) {
    debug("photodiode_driver: Disabling multiplexer\n");
    gpio_set_pin_level(photodiode_config.mux_enable_pin, true); // Active low - HIGH disables
    return SUCCESS;
}

/**
 * \fn read_single_photodiode_adc
 *
 * \brief Read ADC value from a single photodiode channel
 *
 * \param channel channel number to read (0-21)
 * \param value pointer to store the ADC reading
 *
 * \returns status_t SUCCESS if reading was successful
 */
status_t read_single_photodiode_adc(uint8_t channel, uint16_t *value) {
    if (!value || channel >= PHOTODIODE_MAX_COUNT) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    // Set multiplexer to desired channel
    ret_err_status(set_multiplexer_channel(channel), 
                   "photodiode_driver: Failed to set multiplexer channel");
    
    // TODO: Implement actual ADC reading
    // For now, return dummy value with some variation based on channel
    *value = 2000 + (channel * 10); // Dummy value with channel variation
    debug("photodiode_driver: Read channel %d: %d\n", channel, *value);
    
    return SUCCESS;
}

/**
 * \fn test_multiplexer_functionality
 *
 * \brief Test multiplexer functionality by cycling through all channels
 *
 * \returns status_t SUCCESS if test was successful
 */
status_t test_multiplexer_functionality(void) {
    info("photodiode_driver: Starting multiplexer functionality test\n");
    
    // Test all multiplexer channels (0-7)
    for (uint8_t channel = 0; channel < PHOTODIODE_MUX_CHANNELS; channel++) {
        debug("photodiode_driver: Testing channel %d\n", channel);
        
        // Set multiplexer to channel
        status_t result = set_multiplexer_channel(channel);
        if (result != SUCCESS) {
            warning("photodiode_driver: Failed to set channel %d\n", channel);
            return result;
        }
        
        // Small delay to observe the change
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    info("photodiode_driver: Multiplexer test completed successfully\n");
    return SUCCESS;
}

/**
 * \fn test_multiplexer_channel_sequence
 *
 * \brief Test multiplexer by cycling through a specific sequence of channels
 *
 * \returns status_t SUCCESS if test was successful
 */
status_t test_multiplexer_channel_sequence(void) {
    info("photodiode_driver: Starting multiplexer channel sequence test\n");
    
    // Test sequence: 0, 1, 3, 7, 0 (binary: 000, 001, 011, 111, 000)
    uint8_t test_sequence[] = {0, 1, 3, 7, 0};
    uint8_t sequence_length = sizeof(test_sequence) / sizeof(test_sequence[0]);
    
    for (uint8_t i = 0; i < sequence_length; i++) {
        uint8_t channel = test_sequence[i];
        debug("photodiode_driver: Sequence step %d: channel %d\n", i, channel);
        
        status_t result = set_multiplexer_channel(channel);
        if (result != SUCCESS) {
            warning("photodiode_driver: Failed to set channel %d in sequence\n", channel);
            return result;
        }
        
        // Delay to observe the change
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    info("photodiode_driver: Channel sequence test completed successfully\n");
    return SUCCESS;
}
