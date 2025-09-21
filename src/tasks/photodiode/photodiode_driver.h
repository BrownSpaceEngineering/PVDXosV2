#ifndef PHOTODIODE_DRIVER_H
#define PHOTODIODE_DRIVER_H

#include "atmel_start.h"
#include "globals.h"
#include "logging.h"

// Photodiode hardware definitions
#define PHOTODIODE_ADC_CHANNEL_0 0  // ADC channel for photodiode 0
#define PHOTODIODE_ADC_CHANNEL_1 1  // ADC channel for photodiode 1
#define PHOTODIODE_ADC_CHANNEL_2 2  // ADC channel for photodiode 2
#define PHOTODIODE_ADC_CHANNEL_3 3  // ADC channel for photodiode 3
#define PHOTODIODE_ADC_CHANNEL_4 4  // ADC channel for photodiode 4
#define PHOTODIODE_ADC_CHANNEL_5 5  // ADC channel for photodiode 5

// Photodiode calibration constants
#define PHOTODIODE_MAX_ADC_VALUE 4095  // 12-bit ADC maximum value
#define PHOTODIODE_CALIBRATION_FACTOR 1.0f  // Calibration factor (to be determined)

// Function declarations
status_t init_photodiode_hardware(void);
status_t read_photodiode_adc(uint16_t *values, uint8_t count);
status_t calibrate_photodiode_readings(uint16_t *raw_values, float *calibrated_values, uint8_t count);
status_t calculate_sun_vector(float *calibrated_values, float *sun_vector);

#endif // PHOTODIODE_DRIVER_H
