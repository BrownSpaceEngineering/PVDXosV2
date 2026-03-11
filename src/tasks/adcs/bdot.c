/**
 * \file bdot.c
 * 
 * Created: 8 March 2026
 * Authors: Siddharta Laloux, Noah Shepard, Zach Mahan, Ilan Goldfein
 * 
 */

#include "bdot.h"

status_t bdot(mag_data_t *mag_readings, mag_torque_input_t *torques, float k, float dt) {

    if (mag_readings == NULL || torques == NULL) {
        return ERROR_SANITY_CHECK_FAILED;
    }

    torques->x = -k * mag_readings->x * dt;
    torques->y = -k * mag_readings->y * dt;
    torques->z = -k * mag_readings->z * dt;

    return SUCCESS;
} 