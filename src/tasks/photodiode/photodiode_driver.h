#ifndef PHOTODIODE_DRIVER_H
#define PHOTODIODE_DRIVER_H

#include "atmel_start.h"
#include "globals.h"
#include "logging.h"
#include "photodiode_task.h"

// Function declarations
status_t init_photodiode_hardware(void);
status_t read_photodiodes(uint16_t *values);
status_t read_photodiode_adc(uint16_t *values, uint8_t count);
status_t calibrate_photodiode_readings(const uint16_t *raw_values, float *calibrated_values, uint8_t count);
status_t init_multiplexer_gpio(void);
status_t set_multiplexer_channel(uint8_t channel);
status_t enable_multiplexer(void);
status_t disable_multiplexer(void);
status_t read_single_photodiode_adc(uint8_t channel, uint16_t *value);
status_t test_multiplexer_functionality(void);
status_t test_multiplexer_channel_sequence(void);

#endif // PHOTODIODE_DRIVER_H
