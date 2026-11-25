/**
 * adcs_task.c
 *
 * Sensor, timing and actuator glue between PVDXos and the adcs-c `body()` step
 *
 * Created: September 20, 2025
 * Modified: November 13, 2026
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
#include "globals.h"
#include "libraries/adcs/include/body.h"
#include "libraries/adcs/include/photodiode_determination.h"
#include "libraries/adcs/include/quat.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
#include "os_delay.h"
#include "state_machine.h"
#include "task_list.h"
#include "tasks/adcs/adcs_persist.h"
#include "telemetry/telemetry.h"
#include "utc.h"

#define DEG_TO_RAD (3.14159265358979f / 180.0f)
#define MICROTESLA_TO_TESLA 1e-6f
#define SECONDS_PER_DAY 86400.0
#define UNIX_EPOCH_JD 2440587.5

// How often UTC is saved to MRAM. A reboot loses up to this much time, plus however long the reboot takes.
#define ADCS_UTC_SAVE_PERIOD_S 60u
// A saved TLE older than this is not restored, since SGP4's error grows by around a km per day
#define ADCS_TLE_MAX_RESTORE_AGE_DAYS 14.0
// The photodiodes have to agree on sunlight or eclipse for this many steps in a row (about 1 s) before it is reported,
// so that noise near the terminator doesn't flip the state machine's input back and forth
#define ADCS_SUNLIGHT_DEBOUNCE_STEPS 10

// The RM3100 takes about 7 ms to measure all three axes at a cycle count of 200 (see its user manual), so this will
// usually be enough on the first check
#define ADCS_MAG_CONVERSION_MS 8
#define ADCS_MAG_MAX_EXTRA_CHECKS 10

// TODO(blocked on hardware team): confirm these are V/R (the current at 100% duty); lower safety limits would get exceeded
static const float MTQ_FULL_DUTY_CURRENT_A[3] = {0.156f, 0.156f, 0.104f};

// The same values as the uplinked and downlinked tel_adcs_opmode_t. body() runs every step in both; the mode is
// passed to it with body_set_detumble_only(), and body() does all of the control (B-dot, hand-off, pointing).
typedef enum {
    ADCS_MODE_DO_POINT = TEL_ADCS_OPMODE_POINT,              // B-dot until body() hands off to pointing
    ADCS_MODE_DETUMBLE_ONLY = TEL_ADCS_OPMODE_DETUMBLE_ONLY, // B-dot only; body() never hands off
} adcs_mode_t;

// All of this is only touched by the ADCS task itself: commands reach it through its queue
static adcs_mode_t adcs_mode = ADCS_MODE_DO_POINT;

static bool sensor_fault = false; // whether a sensor failed its device check during the current step

static bool in_sunlight = false; // debounced; starts out matching the task manager's default
static uint8_t sunlight_disagree_steps = 0;

static uint32_t last_utc_save_time = 0;

static adcs_tle_t pending_tle; // a TLE waiting for body() to look at it; only valid if tle_pending is set
static bool tle_pending = false;

// Copied out by adcs_get_telemetry(), which runs on other tasks
static tel_adcs_data_t telemetry_snapshot;

/* ---------- ORBIT AND TIME ---------- */

status_t adcs_get_time(int *const unix_time, int *const jd_scalar, float *const jd_frac) {
#ifdef PLANT_SIM
    // body() has to see the plant's simulated time, which advances by a fixed step per tick rather than in real time
    return adcs_mock_get_time(unix_time, jd_scalar, jd_frac);
#endif

    uint32_t utc;
    const status_t result = utc_get(&utc);
    if (result != SUCCESS) {
        return result;
    }

    *unix_time = (int)utc;
    utc_to_jd(utc, jd_scalar, jd_frac);
    return SUCCESS;
}

static status_t offer_tle(const adcs_tle_t *const tle) {
    if (!body_set_tle_metadata(tle->bstar, tle->epoch_jd)) {
        warning("adcs: rejected a TLE with an invalid B* or epoch\n");
        return ERROR_BAD_ARGS;
    }

    pending_tle = *tle;
    tle_pending = true;
    return SUCCESS;
}

void adcs_tle_from_uplink(const uplink_adcs_parameters_t *const params, adcs_tle_t *const tle) {
    // body() takes the mean elements in a different order from TLE line 2, and in radians
    tle->mean[0] = params->mean_motion;
    tle->mean[1] = params->eccentricity;
    tle->mean[2] = params->inclination * DEG_TO_RAD;
    tle->mean[3] = params->raan * DEG_TO_RAD;
    tle->mean[4] = params->argument_of_perigee * DEG_TO_RAD;
    tle->mean[5] = params->mean_anomaly * DEG_TO_RAD;
    tle->bstar = params->bstar;
    tle->epoch_jd = params->epoch_jd;
}

const float *adcs_get_tle_update(void) {
    return tle_pending ? pending_tle.mean : NULL;
}

// Stops offering the pending TLE once body() has looked at it. body() counts every TLE it accepts or rejects, so a
// change in either counter across the body() call means it saw this one; if neither changed (no trusted time yet, or it
// returned early on a bad gyro sample while pointing) the TLE stays pending for the next step. A rejected TLE has used
// up its metadata in body(), so it is dropped rather than retried: the ground resends the whole TLE (tle_last_reject
// says why, see the BODY_ORBIT_* codes in body.h).
static void resolve_pending_tle(const body_sensor_rejects_t *const before, const body_sensor_rejects_t *const after) {
    if (!tle_pending) {
        return;
    }

    if (after->tle_accepted != before->tle_accepted) {
        info("adcs: TLE accepted\n");
        adcs_persist_save_tle(&pending_tle);
        tle_pending = false;
    } else if (after->tle_rejected != before->tle_rejected) {
        warning("adcs: body() rejected the TLE (reason %u), dropped it\n", after->tle_last_reject);
        tle_pending = false;
    }
}

void adcs_restore_state(void) {
    // UTC has to come first, or the TLE would be propagated to the wrong time
    uint32_t saved_utc;
    if (!adcs_persist_load_utc(&saved_utc) || utc_set(saved_utc) != SUCCESS) {
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
    utc_to_jd(saved_utc, &jd_scalar, &jd_frac);
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
    if (utc_get(&utc) == SUCCESS && utc - last_utc_save_time >= ADCS_UTC_SAVE_PERIOD_S) {
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
            body_set_detumble_only(false);
            info("adcs: mode set to do point\n");
            p_cmd->result = SUCCESS;
            break;
        case OPERATION_ADCS_DETUMBLE_ONLY:
            adcs_mode = ADCS_MODE_DETUMBLE_ONLY;
            body_set_detumble_only(true);
            info("adcs: mode set to detumble only\n");
            p_cmd->result = SUCCESS;
            break;
        case OPERATION_ADCS_SET_TLE:
            p_cmd->result = offer_tle(p_cmd->data.adcs_tle);
            break;
        case OPERATION_ADCS_SET_UTC:
            p_cmd->result = utc_set(p_cmd->data.unix_time);
            if (p_cmd->result == SUCCESS) {
                info("adcs: UTC set to %lu\n", (unsigned long)p_cmd->data.unix_time);
                adcs_persist_save_utc(p_cmd->data.unix_time);
                last_utc_save_time = p_cmd->data.unix_time;
            }
            break;
        case OPERATION_SET_KEPLER_COEFF:   // TODO: load from MRAM_REGION_ADCS_UPLINK (UPLINK_MRAM_KEPLER_OFFSET) and apply
        case OPERATION_ADCS_UPDATE_PARAMS: // TODO: load from MRAM_REGION_ADCS_UPLINK (UPLINK_MRAM_ADCS_PARAMS_OFFSET) and apply
        case OPERATION_SET_UNIX_TIME:      // TODO: decide whether the UHF task's per-packet ground timestamp should set UTC
            warning("adcs: operation %d not implemented, ignoring\n", p_cmd->operation);
            p_cmd->result = ERROR_NOT_READY;
            break;
        default:
            fatal("adcs: Invalid operation! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
            break;
    }
}

/* ---------- CONTROL ---------- */

void adcs_compute_currents(const float *const last_mag, const float *const mag, const float *const gyro, const float *const photodiodes,
                           const float dt, float *const currents) {
    // body() does all of the control, in both modes: B-dot detumbling (with its own coil quiet period, before the first
    // TLE, without trusted UTC, and with a failed gyro), the hand-off, pointing, and dropping back to B-dot on a fault
    // or in detumble-only mode. Its currents are used as they are.
    int unix_time = 0;
    int jd_scalar = 0;
    float jd_frac = 0.0f;
    const bool time_valid = adcs_get_time(&unix_time, &jd_scalar, &jd_frac) == SUCCESS;

    // static: the counters struct is a few hundred bytes, and the ADCS task's stack is tight (see adcs_task.h)
    static body_sensor_rejects_t before;
    static body_sensor_rejects_t after;
    body_get_sensor_rejects(&before);
    // Without trusted UTC, body() leaves a pending TLE untouched, so it stays pending until the time is set
    body(last_mag, mag, gyro, photodiodes, adcs_get_tle_update(), unix_time, jd_scalar, jd_frac, time_valid, dt, currents);
    body_get_sensor_rejects(&after);
    resolve_pending_tle(&before, &after);
}

/* ---------- SENSORS ---------- */

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
static status_t read_magnetometer_single(mag_raw_reading_t *const raw, mag_data_t *const reading) {
    // The gain is only set once the device check has passed
    if (!check_device(MAGNETOMETER_ID)) {
        return ERROR_NOT_READY;
    }

    status_t result = mag_modify_interrupts(RM3100_DISABLED, RM3100_SINGLE);
    if (result != SUCCESS) {
        return result;
    }

    os_delay_ms(ADCS_MAG_CONVERSION_MS);
    result = magnetometer_read(raw, reading);
    for (size_t i = 0; result == ERROR_NOT_READY && i < ADCS_MAG_MAX_EXTRA_CHECKS; i++) {
        os_delay_ms(1);
        result = magnetometer_read(raw, reading);
    }
    return result;
}

status_t adcs_read_magnetometer(float *const mag_tesla) {
    mag_raw_reading_t raw;
    mag_data_t reading;
    const status_t result = read_magnetometer_single(&raw, &reading);

    if (result != SUCCESS) {
        handle_read_failure(MAGNETOMETER_ID, "magnetometer");
        for (size_t i = 0; i < 3; i++) {
            mag_tesla[i] = NAN;
        }
        return result;
    }
    telemetry_update_magnetometer_readings(&raw, &reading);

    // Left in the magnetometer's own [U8, U9, U10] axes: body() maps them to the body frame itself (sensor_frames.h)
    mag_tesla[0] = reading.x * MICROTESLA_TO_TESLA;
    mag_tesla[1] = reading.y * MICROTESLA_TO_TESLA;
    mag_tesla[2] = reading.z * MICROTESLA_TO_TESLA;
    return SUCCESS;
}

status_t adcs_read_gyro(float *const gyro_rad_s) {
    SCH1_result_t reading;
    const status_t result = gyro_read(&reading);

    if (result != SUCCESS) {
        telemetry_update_gyro_data(NULL);
        handle_read_failure(GYROSCOPE_ID, "gyro");
        for (size_t i = 0; i < 3; i++) {
            gyro_rad_s[i] = NAN;
        }
        return result;
    }
    telemetry_update_gyro_data(&reading);

    // Left in the gyro chip's own axes: body() maps them to the body frame itself (sensor_frames.h)
    for (size_t i = 0; i < 3; i++) {
        gyro_rad_s[i] = reading.Rate1[i] * DEG_TO_RAD;
    }
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
    telemetry_update_photodiode_data(codes);

    // TODO(blocked on hardware team): confirm PHD_n is wired to PHOTODIODES[n], and that MIN/MAX_READING are in volts
    for (size_t i = 0; i < NUM_DIODES; i++) {
        photodiodes[i] = (float)codes[i] / PHD_ADC_FS * PHD_VREF_V;
    }
    return SUCCESS;
}

// A failed read is all zeros, which would look like eclipse, so it restarts the count instead of counting towards one
void adcs_update_sunlight(const float *const photodiodes, const bool photodiodes_valid) {
    float sun_vector[3];
    if (!photodiodes_valid || get_vec_from_photodiode_readings(photodiodes, sun_vector) == in_sunlight) {
        sunlight_disagree_steps = 0;
        return;
    }

    if (++sunlight_disagree_steps >= ADCS_SUNLIGHT_DEBOUNCE_STEPS) {
        in_sunlight = !in_sunlight;
        sunlight_disagree_steps = 0;
        info("adcs: %s\n", in_sunlight ? "entered sunlight" : "entered eclipse");
    }
}

void adcs_read_sensors(float *const mag_tesla, float *const gyro_rad_s, float *const photodiodes) {
    sensor_fault = false;
    adcs_read_magnetometer(mag_tesla);
    adcs_read_gyro(gyro_rad_s);
    adcs_update_sunlight(photodiodes, adcs_read_photodiodes(photodiodes) == SUCCESS);
}

/* ---------- ACTUATORS, TELEMETRY AND STATE MACHINE ---------- */

void adcs_drive_magnetorquers(const float *const currents) {
    float duty[3];
    for (size_t i = 0; i < 3; i++) {
        duty[i] = isfinite(currents[i]) ? currents[i] / MTQ_FULL_DUTY_CURRENT_A[i] : 0.0f;
    }

    // TODO(blocked on hardware team): confirm positive duty gives a positive moment; a reversed coil makes B-dot spin us up
    mtq_set_all(duty[0], duty[1], duty[2]);
    telemetry_update_magnetorquer_data(&(mag_torque_input_t){.x = duty[0], .y = duty[1], .z = duty[2]});
    debug("adcs: magnetorquer duty [x,y,z]: [%f,%f,%f]\n", duty[0], duty[1], duty[2]);
}

void adcs_publish_telemetry(void) {
    tel_adcs_data_t telemetry = {0};

    // body() runs in both modes, so these stay live in detumble only too
    float q_body_to_eci[4];
    const bool attitude_valid = body_get_attitude(q_body_to_eci);
    if (attitude_valid) {
        quat2rotationvec(q_body_to_eci, telemetry.attitude);
        telemetry.flags |= TEL_ADCS_FLAG_ATTITUDE_VALID;
    }
    if (body_get_position(telemetry.position)) {
        telemetry.flags |= TEL_ADCS_FLAG_POSITION_VALID;
    } else {
        memset(telemetry.position, 0, sizeof(telemetry.position));
    }
    body_get_gyro_bias(telemetry.gyro_bias);
    body_get_covariance_diag(telemetry.covariance_diag);

    adcs_tle_t tle;
    if (body_get_tle(tle.mean, &tle.bstar, &tle.epoch_jd)) {
        telemetry.flags |= TEL_ADCS_FLAG_TLE_LOADED;
        const double epoch_unix_time = (tle.epoch_jd - UNIX_EPOCH_JD) * SECONDS_PER_DAY;
        telemetry.tle_epoch_unix_time = epoch_unix_time > 0.0 && epoch_unix_time < 4294967296.0 ? (uint32_t)epoch_unix_time : 0;
    }

    uint32_t utc;
    if (utc_get(&utc) == SUCCESS) {
        telemetry.flags |= TEL_ADCS_FLAG_UTC_VALID;
    }
    if (adcs_mode == ADCS_MODE_DO_POINT && body_is_pointing()) {
        telemetry.flags |= TEL_ADCS_FLAG_POINTING;
    }
    if (in_sunlight) {
        telemetry.flags |= TEL_ADCS_FLAG_IN_SUNLIGHT;
    }
    telemetry.opmode = (uint8_t)adcs_mode;
    const int filter_resets = body_get_filter_resets();
    telemetry.filter_resets = filter_resets > UINT16_MAX ? UINT16_MAX : (uint16_t)filter_resets;

    taskENTER_CRITICAL();
    telemetry_snapshot = telemetry;
    taskEXIT_CRITICAL();
    telemetry_update_adcs_data(&telemetry);

    debug("adcs: mode: %d, pointing: %d, attitude valid: %d, filter resets: %d, min free stack: %lu words\n", adcs_mode, body_is_pointing(),
          attitude_valid, body_get_filter_resets(), (unsigned long)uxTaskGetStackHighWaterMark(NULL));
}

void adcs_get_telemetry(tel_adcs_data_t *const telemetry) {
    taskENTER_CRITICAL();
    *telemetry = telemetry_snapshot;
    taskEXIT_CRITICAL();
}

void adcs_update_state_inputs(void) {
    static input_update_data_t adcs_inputs = {
        .input_update_bitmask =
            INPUT_UPDATE_BIT(ADCS_POINTING_UPDATE) | INPUT_UPDATE_BIT(ADCS_SENSOR_FAULT_UPDATE) | INPUT_UPDATE_BIT(SUNLIGHT_STATUS_UPDATE),
    };
    static bool sent_pointing = false;
    static bool sent_sensor_fault = false;
    static bool sent_sunlight = false;

    const bool pointing = adcs_mode == ADCS_MODE_DO_POINT && body_is_pointing();
    if (pointing == sent_pointing && sensor_fault == sent_sensor_fault && in_sunlight == sent_sunlight) {
        return;
    }

    adcs_inputs.new_values.adcs_pointing = pointing;
    adcs_inputs.new_values.adcs_sensor_fault = sensor_fault;
    adcs_inputs.new_values.in_sunlight = in_sunlight;
    command_t cmd = get_state_input_update_command(&adcs_inputs);
    // If the task manager's queue is full, this tries again on the next step
    if (enqueue_command(&cmd) == SUCCESS) {
        sent_pointing = pointing;
        sent_sensor_fault = sensor_fault;
        sent_sunlight = in_sunlight;
    }
}
