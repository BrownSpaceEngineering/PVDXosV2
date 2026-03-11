
#ifndef ADCS_BDOT_H
#define ADCS_BDOT_H

#include "drivers/magnetometer/magnetometer_driver.h"
#include "drivers/magnetorquer/magnetorquer_driver.h"
#include "globals.h"

status_t bdot(mag_data_t *mag_readings, mag_torque_input_t *torques, float k, float dt);

#endif
