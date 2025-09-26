#ifndef PHOTODIODE_DRIVER_H
#define PHOTODIODE_DRIVER_H

#include "atmel_start.h"
#include "globals.h"
#include "logging.h"
#include "photodiode_task.h"

// Photodiode calibration constants
#define PHOTODIODE_MAX_ADC_VALUE 4095  // 12-bit ADC maximum value
#define PHOTODIODE_CALIBRATION_FACTOR 1.0f  // Calibration factor (to be determined)

// Function declarations
status_t init_photodiode_hardware(void);
status_t read_photodiode_adc(uint16_t *values, uint8_t count);
status_t calibrate_photodiode_readings(uint16_t *raw_values, float *calibrated_values, uint8_t count);
status_t calculate_sun_vector(float *calibrated_values, float *sun_vector);

#endif // PHOTODIODE_DRIVER_H
