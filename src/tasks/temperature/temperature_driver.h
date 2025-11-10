#ifndef TEMPERATURE_DRIVER_H
#define TEMPERATURE_DRIVER_H

#include "atmel_start.h"
#include "globals.h"

typedef struct {
    uint16_t ptat_raw;
    uint16_t ctat_raw;
    float temperature_c;
    float temperature_ptat_c;
    float temperature_ctat_c;
} temp_sensor_sample_t;

status_t temp_sensor_init(void);
status_t temp_sensor_sample(temp_sensor_sample_t *sample);
status_t temp_sensor_get_celsius(float *temperature_c);

#endif // TEMPERATURE_DRIVER_H

