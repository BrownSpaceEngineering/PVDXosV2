/**
 * adcs_task.c
 *
 * Sensor, timing and actuator glue between PVDXos and the adcs-c `body()` step
 *
 * Created: September 20, 2025
 * Modified: October 25, 2026
 * Authors: Avinash Patel, Yi Lyo, Alexander Thaep, Zach Mahan, Siddharta Laloux
 */

#include "tasks/adcs/adcs_task.h"

#include <math.h>
#include <string.h>

#include "checks/device_checks.h"
#include "drivers/gyro/gyro_driver.h"
#include "drivers/rtc/rtc_driver.h"
#include "globals.h"
#include "libraries/adcs/include/bdot.h"
#include "libraries/adcs/include/body.h"
#include "libraries/adcs/include/moments2currents.h"
#include "libraries/adcs/include/photodiode_determination.h"
#include "logging.h"
#include "task_list.h"

#define DEG_TO_RAD (3.14159265358979f / 180.0f)
#define SECONDS_PER_DAY 86400
#define UNIX_EPOCH_JD_SCALAR 2440587 // The Unix epoch is JD 2440587.5
#define UNIX_EPOCH_JD_FRAC 0.5f

// TODO: remove once the RTC is synced to UTC; arbitrary placeholder (2026-01-01T00:00:00Z)
#define ADCS_PLACEHOLDER_UNIX_EPOCH 1767225600

typedef enum {
    ADCS_MODE_DO_POINT,      // body() whenever possible, B-dot whenever it is not pointing
    ADCS_MODE_DETUMBLE_ONLY, // B-dot only; body() is not called
} adcs_mode_t;

// Only touched by the ADCS task itself: commands reach it through its queue
static adcs_mode_t adcs_mode = ADCS_MODE_DO_POINT;

/* ---------- COMMANDS ---------- */

command_t get_adcs_do_point_command(void) {
    return (command_t){
        .target = p_adcs_task,
        .operation = OPERATION_ADCS_DO_POINT,
        .data_type = CMD_DATA_NONE,
        .result = PROCESSING,
    };
}

command_t get_adcs_detumble_only_command(void) {
    return (command_t){
        .target = p_adcs_task,
        .operation = OPERATION_ADCS_DETUMBLE_ONLY,
        .data_type = CMD_DATA_NONE,
        .result = PROCESSING,
    };
}

void exec_command_adcs(command_t *const p_cmd) {
    if (p_cmd->target != p_adcs_task) {
        fatal("adcs: command target is not adcs! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
    }

    switch (p_cmd->operation) {
        case OPERATION_ADCS_DO_POINT:
            adcs_mode = ADCS_MODE_DO_POINT;
            info("adcs: mode set to do point\n");
            p_cmd->result = SUCCESS;
            break;
        case OPERATION_ADCS_DETUMBLE_ONLY:
            adcs_mode = ADCS_MODE_DETUMBLE_ONLY;
            info("adcs: mode set to detumble only\n");
            p_cmd->result = SUCCESS;
            break;
        default:
            fatal("adcs: Invalid operation! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
            break;
    }
}

/* ---------- CONTROL ---------- */

// Same requirement as body()'s own magnetometer check: finite and non-zero
static bool mag_usable(const float *const mag) {
    const float norm_squared = mag[0] * mag[0] + mag[1] * mag[1] + mag[2] * mag[2];
    return isfinite(norm_squared) && norm_squared > 0.0f;
}

static void adcs_detumble(const float *const last_mag, const float *const mag, const float dt, float *const currents) {
    // moment2current3axis's clamp lets NaN through, so a bad reading has to be stopped here
    if (!mag_usable(last_mag) || !mag_usable(mag)) {
        memset(currents, 0, 3 * sizeof(float));
        return;
    }

    float moments[3];
    Bdot(mag, last_mag, BDOT_GAIN, dt, moments);
    moment2current3axis(moments, Imax, currents);
}

void adcs_compute_currents(const float *const last_mag, const float *const mag, const float *const gyro, const float *const photodiodes,
                           const float dt, float *const currents) {
    if (adcs_mode == ADCS_MODE_DO_POINT) {
        int unix_time;
        int jd_scalar;
        float jd_frac;

        if (adcs_get_time(&unix_time, &jd_scalar, &jd_frac) == SUCCESS) {
            body(last_mag, mag, gyro, photodiodes, adcs_get_tle_update(), unix_time, jd_scalar, jd_frac, dt, currents);
            if (body_is_pointing()) {
                return;
            }
        }
    }

    // Not pointing. body() may already have run this same B-dot, but it also gives up with zero currents for reasons
    // that don't stop B-dot (no TLE yet, a rejected TLE, a failed gyro read), and it can't tell us which happened.
    adcs_detumble(last_mag, mag, dt, currents);
}

/* ---------- SENSORS ---------- */

status_t adcs_read_magnetometer(float *const mag_tesla) {
    // TODO: use RM3100 driver -> call magnetometer_read(), convert its gain-adjusted output (uT) to Tesla, and rotate it from the sensor
    // frame into the body frame
    for (size_t i = 0; i < 3; i++) {
        mag_tesla[i] = NAN;
    }
    return ERROR_NOT_READY;
}

status_t adcs_read_gyro(float *const gyro_rad_s) {
    SCH1_result_t reading;
    status_t result = gyro_read(&reading);

    if (result != SUCCESS) {
        warning("adcs: gyro read failed\n");
        // uncheck device to force the check function to run again
        uncheck_device(GYROSCOPE_ID);
        if (!check_device(GYROSCOPE_ID)) {
            warning("adcs: gyro device check failed after read failure\n");
            // TODO: queue something to the task manager to update the state machine inputs
        }
        for (size_t i = 0; i < 3; i++) {
            gyro_rad_s[i] = NAN;
        }
        return result;
    }

    // TODO: the SCH16T reports rates about its own package axes, but body() expects the satellite body axes (the
    // PHOTODIODES / magnetorquer frame). Apply the fixed rotation R (omega_body = R * omega_sensor, usually a signed
    // permutation) from the PCB/CAD mounting and the datasheet axis diagram; if the chip is aligned, R is the identity.
    for (size_t i = 0; i < 3; i++) {
        gyro_rad_s[i] = reading.Rate1[i] * DEG_TO_RAD;
    }
    return SUCCESS;
}

status_t adcs_read_photodiodes(float *const photodiodes) {
    // TODO: the photodiode driver is commented out. Once it is restored, call photodiode_read() and map
    // its PHOTODIODE_COUNT raw ADC channels onto the NUM_DIODES entries of PHOTODIODES (same order and
    // scaling as MIN_READING / MAX_READING in adcs-c).
    memset(photodiodes, 0, NUM_DIODES * sizeof(float));
    return ERROR_NOT_READY;
}

/* ---------- ORBIT AND TIME ---------- */

const float *adcs_get_tle_update(void) {
    // TODO: return a TLE uplinked from the ground (TEL_CMD_ADCS_UPDATE_PARAMETERS), consumed once, and call
    // body_set_tle_metadata() with its B* and epoch first. Until the first TLE arrives body() has no orbit
    // to propagate and returns zero currents every step, so do point falls back to B-dot.
    // TODO: the uplink must carry the elements, B* and the TLE's real epoch together; reject it if any are missing,
    // since body() otherwise treats the elements as valid "now" (and B* as 0). Send the epoch as a double or an
    // integer JD + fraction, never a float (a float JD only resolves to ~0.25 day).
    return NULL;
}

status_t adcs_get_time(int *const unix_time, int *const jd_scalar, float *const jd_frac) {
    rtc_data_t rtc;
    status_t result = get_rtc_values(&rtc);
    if (result != SUCCESS) {
        warning("adcs: failed to read RTC\n");
        return result;
    }

    // TODO: the RTC only counts time since boot
    // Replace the placeholder epoch with a UTC time synced from the ground (or the radio) once possible
    // TODO: UTC must also survive a reboot (e.g. saved to MRAM and restored), otherwise a restored TLE is
    // propagated to the wrong time.

    *unix_time = ADCS_PLACEHOLDER_UNIX_EPOCH + (int)rtc.seconds_count;

    int days = *unix_time / SECONDS_PER_DAY;
    int seconds_into_day = *unix_time % SECONDS_PER_DAY;
    *jd_scalar = UNIX_EPOCH_JD_SCALAR + days;
    *jd_frac = UNIX_EPOCH_JD_FRAC + (float)seconds_into_day / SECONDS_PER_DAY;
    if (*jd_frac >= 1.0f) {
        *jd_scalar += 1;
        *jd_frac -= 1.0f;
    }
    return SUCCESS;
}

void adcs_drive_magnetorquers(const float *const currents) {
    // TODO: hand these currents to magtorq driver once possible, and check MAGNETORQUERS_ID on failure like the sensors do.
    debug("adcs: magnetorquer currents [x,y,z]: [%f,%f,%f] A\n", currents[0], currents[1], currents[2]);
}

void adcs_publish_telemetry(void) {
    float q_body_to_eci[4];
    bool attitude_valid = body_get_attitude(q_body_to_eci);

    // these are frozen while in detumble only, since body() is not running
    debug("adcs: mode: %d, pointing: %d, attitude valid: %d, filter resets: %d\n", adcs_mode, body_is_pointing(), attitude_valid,
          body_get_filter_resets());

    // TODO: fill tel_adcs_data_t (attitude, gyro bias from body_get_gyro_bias(), position) and send the
    // pointing state to the task manager as a state machine input.
    // TODO: adcs-c has no position getter; r_eci is a local inside body(). Add one (e.g. body_get_position()).
    (void)attitude_valid;
}
