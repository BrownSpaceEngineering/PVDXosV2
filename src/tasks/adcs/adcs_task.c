/**
 * adcs_task.c
 *
 * Sensor, timing and actuator glue between PVDXos and the adcs-c `body()` step
 *
 * Created: September 20, 2025
 * Modified: October 30, 2026
 * Authors: Avinash Patel, Yi Lyo, Alexander Thaep, Zach Mahan, Siddharta Laloux
 */

#include "tasks/adcs/adcs_task.h"

#include <math.h>
#include <string.h>

#include "checks/device_checks.h"
#include "drivers/gyro/gyro_driver.h"
#include "drivers/magnetometer/magnetometer_driver.h"
#include "drivers/magnetorquer/magnetorquer_driver.h"
#include "drivers/photodiode/photodiode_driver.h"
#include "drivers/rtc/rtc_driver.h"
#include "globals.h"
#include "libraries/adcs/include/bdot.h"
#include "libraries/adcs/include/body.h"
#include "libraries/adcs/include/moments2currents.h"
#include "libraries/adcs/include/photodiode_determination.h"
#include "libraries/adcs/include/quat.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
#include "state_machine.h"
#include "task_list.h"
#include "tasks/adcs/adcs_persist.h"

#define DEG_TO_RAD (3.14159265358979f / 180.0f)
#define MICROTESLA_TO_TESLA 1e-6f
#define SECONDS_PER_DAY 86400
#define UNIX_EPOCH_JD_SCALAR 2440587 // The Unix epoch is JD 2440587.5
#define UNIX_EPOCH_JD_FRAC 0.5f

// The RTC counts at 32768 Hz (2^15), so one wrap of its 32-bit counter is 2^17 seconds, or about 36 hours
#define RTC_COUNTS_PER_SECOND_SHIFT 15
#define RTC_WRAP_SECONDS_SHIFT 17

// A UTC earlier than this can't be real (2026-01-01T00:00:00Z)
#define ADCS_MIN_VALID_UNIX_TIME 1767225600u
// How often UTC is saved to MRAM. A reboot loses up to this much time, plus however long the reboot takes.
#define ADCS_UTC_SAVE_PERIOD_S 60u
// A saved TLE older than this is not restored, since SGP4's error grows by around a km per day
#define ADCS_TLE_MAX_RESTORE_AGE_DAYS 14.0
// body() checks a TLE as soon as the sensors let it get that far, so a TLE that fails this many times is bad. It has
// to be dropped: body() gives up on any step where it is handed a bad TLE, which would stop pointing for good.
#define ADCS_TLE_MAX_ATTEMPTS 3

// The RM3100 takes about 7 ms to measure all three axes at a cycle count of 200 (see its user manual), so this will
// usually be enough on the first check
#define ADCS_MAG_CONVERSION_MS 8
#define ADCS_MAG_MAX_EXTRA_CHECKS 10

// TODO(blocked on hardware team): confirm these are V/R (the current at 100% duty); lower safety limits would get exceeded
static const float MTQ_FULL_DUTY_CURRENT_A[3] = {0.156f, 0.156f, 0.104f};

// TODO(blocked on mechanical): set these sensor-to-body rotations from the PCB/CAD mounting (identity until then)
static const float BODY_FROM_MAG[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
static const float BODY_FROM_GYRO[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};

typedef enum {
    ADCS_MODE_DO_POINT,      // body() whenever possible, B-dot whenever it is not pointing
    ADCS_MODE_DETUMBLE_ONLY, // B-dot only; body() is not called
} adcs_mode_t;

// All of this is only touched by the ADCS task itself: commands reach it through its queue
static adcs_mode_t adcs_mode = ADCS_MODE_DO_POINT;

static bool sensor_fault = false; // whether a sensor failed its device check during the current step

static bool utc_valid = false;
static uint32_t utc_at_rtc_zero = 0; // UTC when the RTC read zero
static uint32_t last_rtc_count = 0;  // for detecting when the RTC wraps
static uint32_t rtc_wraps = 0;       //
static uint32_t last_utc_save_time = 0;

static adcs_tle_t pending_tle; // a TLE waiting for body() to accept it; only valid if tle_pending is set
static bool tle_pending = false;
static uint8_t tle_attempts = 0;

// Copied out by adcs_get_telemetry(), which runs on other tasks
static tel_adcs_data_t telemetry_snapshot;

/* ---------- ORBIT AND TIME ---------- */

// Fails only if the RTC can't be read. Must be called at least once per RTC wrap, which the control loop does.
static status_t get_uptime_seconds(uint32_t *const uptime_s) {
    rtc_data_t rtc;
    const status_t result = get_rtc_values(&rtc);
    if (result != SUCCESS) {
        warning("adcs: failed to read RTC\n");
        return result;
    }

    if (rtc.rtc_count < last_rtc_count) {
        rtc_wraps++;
    }
    last_rtc_count = rtc.rtc_count;

    *uptime_s = (rtc_wraps << RTC_WRAP_SECONDS_SHIFT) + (rtc.rtc_count >> RTC_COUNTS_PER_SECOND_SHIFT);
    return SUCCESS;
}

static void unix_to_jd(const uint32_t unix_time, int *const jd_scalar, float *const jd_frac) {
    *jd_scalar = UNIX_EPOCH_JD_SCALAR + (int)(unix_time / SECONDS_PER_DAY);
    *jd_frac = UNIX_EPOCH_JD_FRAC + (float)(unix_time % SECONDS_PER_DAY) / SECONDS_PER_DAY;
    if (*jd_frac >= 1.0f) {
        *jd_scalar += 1;
        *jd_frac -= 1.0f;
    }
}

static status_t set_utc(const uint32_t unix_time) {
    if (unix_time < ADCS_MIN_VALID_UNIX_TIME) {
        warning("adcs: rejected implausible UTC %lu\n", (unsigned long)unix_time);
        return ERROR_BAD_ARGS;
    }

    uint32_t uptime_s;
    const status_t result = get_uptime_seconds(&uptime_s);
    if (result != SUCCESS) {
        return result;
    }

    utc_at_rtc_zero = unix_time - uptime_s;
    utc_valid = true;
    return SUCCESS;
}

// TODO: move UTC out of ADCS once other tasks need it (e.g. telemetry timestamps)
static status_t get_utc(uint32_t *const unix_time) {
    if (!utc_valid) {
        return ERROR_NOT_READY;
    }

    uint32_t uptime_s;
    const status_t result = get_uptime_seconds(&uptime_s);
    if (result != SUCCESS) {
        return result;
    }

    *unix_time = utc_at_rtc_zero + uptime_s;
    return SUCCESS;
}

status_t adcs_get_time(int *const unix_time, int *const jd_scalar, float *const jd_frac) {
    uint32_t utc;
    const status_t result = get_utc(&utc);
    if (result != SUCCESS) {
        return result;
    }

    *unix_time = (int)utc;
    unix_to_jd(utc, jd_scalar, jd_frac);
    return SUCCESS;
}

static status_t offer_tle(const adcs_tle_t *const tle) {
    if (!body_set_tle_metadata(tle->bstar, tle->epoch_jd)) {
        warning("adcs: rejected a TLE with an invalid B* or epoch\n");
        return ERROR_BAD_ARGS;
    }

    pending_tle = *tle;
    tle_pending = true;
    tle_attempts = 0;
    return SUCCESS;
}

const float *adcs_get_tle_update(void) {
    return tle_pending ? pending_tle.mean : NULL;
}

// Stops offering the pending TLE once body() has either accepted it or had enough chances to
static void resolve_pending_tle(const bool body_saw_tle) {
    if (!tle_pending) {
        return;
    }

    adcs_tle_t committed;
    const bool committed_valid = body_get_tle(committed.mean, &committed.bstar, &committed.epoch_jd);
    if (committed_valid && memcmp(committed.mean, pending_tle.mean, sizeof(committed.mean)) == 0 &&
        memcmp(&committed.epoch_jd, &pending_tle.epoch_jd, sizeof(committed.epoch_jd)) == 0) {
        info("adcs: TLE accepted\n");
        adcs_persist_save_tle(&pending_tle);
        tle_pending = false;
    } else if (body_saw_tle && ++tle_attempts >= ADCS_TLE_MAX_ATTEMPTS) {
        warning("adcs: dropped a TLE that body() rejected\n");
        tle_pending = false;
    }
}

void adcs_restore_state(void) {
    if (!adcs_persist_init()) {
        warning("adcs: MRAM unavailable; TLE and UTC won't survive a reboot\n");
        return;
    }

    // UTC has to come first, or the TLE would be propagated to the wrong time
    uint32_t saved_utc;
    if (!adcs_persist_load_utc(&saved_utc) || set_utc(saved_utc) != SUCCESS) {
        info("adcs: no saved UTC to restore\n");
        return;
    }
    last_utc_save_time = saved_utc;
    info("adcs: restored UTC %lu\n", (unsigned long)saved_utc);

    adcs_tle_t saved_tle;
    if (!adcs_persist_load_tle(&saved_tle)) {
        info("adcs: no saved TLE to restore\n");
        return;
    }

    int jd_scalar;
    float jd_frac;
    unix_to_jd(saved_utc, &jd_scalar, &jd_frac);
    const double tle_age_days = fabs((double)jd_scalar + (double)jd_frac - saved_tle.epoch_jd);
    if (!(tle_age_days <= ADCS_TLE_MAX_RESTORE_AGE_DAYS)) {
        warning("adcs: saved TLE is too old to restore\n");
        return;
    }

    // body() has no separate restore path: a restored TLE is checked exactly like an uplinked one
    if (offer_tle(&saved_tle) == SUCCESS) {
        info("adcs: restored saved TLE\n");
    }
}

void adcs_save_state(void) {
    uint32_t utc;
    if (get_utc(&utc) == SUCCESS && utc - last_utc_save_time >= ADCS_UTC_SAVE_PERIOD_S) {
        adcs_persist_save_utc(utc);
        last_utc_save_time = utc;
    }
}

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

command_t get_adcs_set_tle_command(const adcs_tle_t *const tle) {
    return (command_t){
        .target = p_adcs_task,
        .operation = OPERATION_ADCS_SET_TLE,
        .data.adcs_tle = tle,
        .data_type = CMD_DATA_ADCS_TLE,
        .result = PROCESSING,
    };
}

command_t get_adcs_set_utc_command(const uint32_t unix_time) {
    return (command_t){
        .target = p_adcs_task,
        .operation = OPERATION_ADCS_SET_UTC,
        .data.unix_time = unix_time,
        .data_type = CMD_DATA_UNIX_TIME,
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
        case OPERATION_ADCS_SET_TLE:
            p_cmd->result = offer_tle(p_cmd->data.adcs_tle);
            break;
        case OPERATION_ADCS_SET_UTC:
            p_cmd->result = set_utc(p_cmd->data.unix_time);
            if (p_cmd->result == SUCCESS) {
                info("adcs: UTC set to %lu\n", (unsigned long)p_cmd->data.unix_time);
                adcs_persist_save_utc(p_cmd->data.unix_time);
                last_utc_save_time = p_cmd->data.unix_time;
            }
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

            // body() bails before it looks at the TLE if the magnetometer or gyro readings are unusable
            const bool gyro_finite = isfinite(gyro[0]) && isfinite(gyro[1]) && isfinite(gyro[2]);
            resolve_pending_tle(gyro_finite && mag_usable(last_mag) && mag_usable(mag));

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

static void rotate_to_body(const float rotation[3][3], const float *const sensor_frame, float *const body_frame) {
    for (size_t row = 0; row < 3; row++) {
        body_frame[row] = rotation[row][0] * sensor_frame[0] + rotation[row][1] * sensor_frame[1] + rotation[row][2] * sensor_frame[2];
    }
}

static void handle_read_failure(const device_id_t device, const char *const name) {
    warning("adcs: %s read failed\n", name);
    // at this point we should check the device, as we've potentially run into a fault
    // uncheck device to force the check function to run again
    uncheck_device(device);
    if (!check_device(device)) {
        warning("adcs: %s device check failed after read failure\n", name);
        sensor_fault = true;
    }
}

// Commanding a single measurement here means that it only happens while the magnetorquers are off
static status_t read_magnetometer_single(mag_data_t *const reading) {
    // The gain is only set once the device check has passed
    if (!check_device(MAGNETOMETER_ID)) {
        return ERROR_NOT_READY;
    }

    status_t result = mag_modify_interrupts(RM3100_DISABLED, RM3100_SINGLE);
    if (result != SUCCESS) {
        return result;
    }

    vTaskDelay(pdMS_TO_TICKS(ADCS_MAG_CONVERSION_MS));
    result = magnetometer_read(NULL, reading);
    for (size_t i = 0; result == ERROR_NOT_READY && i < ADCS_MAG_MAX_EXTRA_CHECKS; i++) {
        vTaskDelay(pdMS_TO_TICKS(1));
        result = magnetometer_read(NULL, reading);
    }
    return result;
}

status_t adcs_read_magnetometer(float *const mag_tesla) {
    mag_data_t reading;
    const status_t result = read_magnetometer_single(&reading);

    if (result != SUCCESS) {
        handle_read_failure(MAGNETOMETER_ID, "magnetometer");
        for (size_t i = 0; i < 3; i++) {
            mag_tesla[i] = NAN;
        }
        return result;
    }

    const float sensor_frame[3] = {reading.x * MICROTESLA_TO_TESLA, reading.y * MICROTESLA_TO_TESLA, reading.z * MICROTESLA_TO_TESLA};
    rotate_to_body(BODY_FROM_MAG, sensor_frame, mag_tesla);
    return SUCCESS;
}

status_t adcs_read_gyro(float *const gyro_rad_s) {
    SCH1_result_t reading;
    const status_t result = gyro_read(&reading);

    if (result != SUCCESS) {
        handle_read_failure(GYROSCOPE_ID, "gyro");
        for (size_t i = 0; i < 3; i++) {
            gyro_rad_s[i] = NAN;
        }
        return result;
    }

    const float sensor_frame[3] = {reading.Rate1[0] * DEG_TO_RAD, reading.Rate1[1] * DEG_TO_RAD, reading.Rate1[2] * DEG_TO_RAD};
    rotate_to_body(BODY_FROM_GYRO, sensor_frame, gyro_rad_s);
    return SUCCESS;
}

status_t adcs_read_photodiodes(float *const photodiodes) {
    uint16_t codes[PHD_COUNT];
    const status_t result = phd_read_all(codes);

    if (result != SUCCESS) {
        handle_read_failure(PHOTODIODE_ID, "photodiode");
        memset(photodiodes, 0, NUM_DIODES * sizeof(float));
        return result;
    }

    // TODO(blocked on hardware team): confirm PHD_n is wired to PHOTODIODES[n], and that MIN/MAX_READING are in volts
    for (size_t i = 0; i < NUM_DIODES; i++) {
        photodiodes[i] = (float)codes[i] / PHD_ADC_FS * PHD_VREF_V;
    }
    return SUCCESS;
}

void adcs_read_sensors(float *const mag_tesla, float *const gyro_rad_s, float *const photodiodes) {
    sensor_fault = false;
    adcs_read_magnetometer(mag_tesla);
    adcs_read_gyro(gyro_rad_s);
    adcs_read_photodiodes(photodiodes);
}

/* ---------- ACTUATORS, TELEMETRY AND STATE MACHINE ---------- */

void adcs_drive_magnetorquers(const float *const currents) {
    float duty[3];
    for (size_t i = 0; i < 3; i++) {
        duty[i] = isfinite(currents[i]) ? currents[i] / MTQ_FULL_DUTY_CURRENT_A[i] : 0.0f;
    }

    // TODO(blocked on hardware team): confirm positive duty gives a positive moment; a reversed coil makes B-dot spin us up
    mtq_set_all(duty[0], duty[1], duty[2]);
    debug("adcs: magnetorquer duty [x,y,z]: [%f,%f,%f]\n", duty[0], duty[1], duty[2]);
}

void adcs_publish_telemetry(void) {
    tel_adcs_data_t telemetry = {0};

    // these are frozen while in detumble only, since body() is not running
    float q_body_to_eci[4];
    const bool attitude_valid = body_get_attitude(q_body_to_eci);
    if (attitude_valid) {
        quat2rotationvec(q_body_to_eci, telemetry.attitude);
    }
    if (!body_get_position(telemetry.position)) {
        memset(telemetry.position, 0, sizeof(telemetry.position));
    }
    // TODO(blocked on telemetry format): fill the rest of state[] and covariance[], which don't match the filter's shapes
    body_get_gyro_bias(&telemetry.state[3]);

    taskENTER_CRITICAL();
    telemetry_snapshot = telemetry;
    taskEXIT_CRITICAL();

    debug("adcs: mode: %d, pointing: %d, attitude valid: %d, filter resets: %d, min free stack: %lu words\n", adcs_mode, body_is_pointing(),
          attitude_valid, body_get_filter_resets(), (unsigned long)uxTaskGetStackHighWaterMark(NULL));
}

void adcs_get_telemetry(tel_adcs_data_t *const telemetry) {
    taskENTER_CRITICAL();
    *telemetry = telemetry_snapshot;
    taskEXIT_CRITICAL();
}

void adcs_update_state_inputs(void) {
    // TODO(blocked on state table): report SUNLIGHT_STATUS_UPDATE once STATE_PEROVSKITE_CAPTURE exists; idle would flip to it and back
    static input_update_data_t adcs_inputs = {
        .input_update_bitmask = INPUT_UPDATE_BIT(ADCS_POINTING_UPDATE) | INPUT_UPDATE_BIT(ADCS_SENSOR_FAULT_UPDATE),
    };
    static bool sent_pointing = false;
    static bool sent_sensor_fault = false;

    const bool pointing = adcs_mode == ADCS_MODE_DO_POINT && body_is_pointing();
    if (pointing == sent_pointing && sensor_fault == sent_sensor_fault) {
        return;
    }

    adcs_inputs.new_values.adcs_pointing = pointing;
    adcs_inputs.new_values.adcs_sensor_fault = sensor_fault;
    command_t cmd = get_state_input_update_command(&adcs_inputs);
    // If the task manager's queue is full, this tries again on the next step
    if (enqueue_command(&cmd) == SUCCESS) {
        sent_pointing = pointing;
        sent_sensor_fault = sensor_fault;
    }
}
