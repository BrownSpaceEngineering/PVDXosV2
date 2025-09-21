#ifndef PHOTODIODE_DRIVER_H
#define PHOTODIODE_DRIVER_H

#include "atmel_start.h"
#include "globals.h"
#include "logging.h"
#include "photodiode_task.h"

// Photodiode hardware definitions
#define PHOTODIODE_MAX_ADC_CHANNELS 16  // Maximum ADC channels available
#define PHOTODIODE_MULTIPLEX_CHANNELS 21 // Can multiplex to support up to 21 photodiodes

// ADC channel definitions (0-15 for direct access, 16+ for multiplexed)
#define PHOTODIODE_ADC_CHANNEL_0 0
#define PHOTODIODE_ADC_CHANNEL_1 1
#define PHOTODIODE_ADC_CHANNEL_2 2
#define PHOTODIODE_ADC_CHANNEL_3 3
#define PHOTODIODE_ADC_CHANNEL_4 4
#define PHOTODIODE_ADC_CHANNEL_5 5
#define PHOTODIODE_ADC_CHANNEL_6 6
#define PHOTODIODE_ADC_CHANNEL_7 7
#define PHOTODIODE_ADC_CHANNEL_8 8
#define PHOTODIODE_ADC_CHANNEL_9 9
#define PHOTODIODE_ADC_CHANNEL_10 10
#define PHOTODIODE_ADC_CHANNEL_11 11
#define PHOTODIODE_ADC_CHANNEL_12 12
#define PHOTODIODE_ADC_CHANNEL_13 13
#define PHOTODIODE_ADC_CHANNEL_14 14
#define PHOTODIODE_ADC_CHANNEL_15 15

// Photodiode calibration constants
#define PHOTODIODE_MAX_ADC_VALUE 4095  // 12-bit ADC maximum value
#define PHOTODIODE_CALIBRATION_FACTOR 1.0f  // Calibration factor (to be determined)

// Multiplexing support
#define PHOTODIODE_MUX_ENABLE_PIN 0    // GPIO pin for multiplexer enable
#define PHOTODIODE_MUX_SELECT_PINS 4   // Number of GPIO pins for multiplexer selection

// Function declarations
status_t init_photodiode_hardware(void);
status_t read_photodiode_adc(uint16_t *values, uint8_t count, const uint8_t *channel_map);
status_t read_photodiode_adc_multiplexed(uint16_t *values, uint8_t count, const uint8_t *channel_map);
status_t calibrate_photodiode_readings(uint16_t *raw_values, float *calibrated_values, uint8_t count);
status_t calculate_sun_vector(float *calibrated_values, float *sun_vector, uint8_t count);
status_t set_photodiode_sample_rate(float sample_rate_hz);

#endif // PHOTODIODE_DRIVER_H
