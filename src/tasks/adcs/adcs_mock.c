/**
 * adcs_mock.c
 *
 * Hardware-in-the-loop-free test of the ADCS integration, built with `make dev ADCS_MOCK=1`. The adcs-c truth plant
 * (orbit, attitude dynamics, sensor noise, magnetorquers) stands in for the real sensors and coils, so the ADCS task
 * runs its normal control path (commands, TLE handling, `adcs_compute_currents()`, telemetry, state machine inputs)
 * under the scheduler, and its pointing is scored against the plant's truth over RTT.
 *
 * Created: November 8, 2026
 * Authors: Zach Mahan
 */

#ifdef ADCS_MOCK

    #include <math.h>
    #include <string.h>

    #include "globals.h"
    #include "libraries/adcs/include/body.h"
    #include "libraries/adcs/include/plant.h"
    #include "libraries/cmd_dispatcher/cmd_dispatcher.h"
    #include "logging.h"
    #include "tasks/adcs/adcs_task.h"

    #define MOCK_PI 3.14159265358979323846
    #define MOCK_RAD_TO_DEG (180.0 / MOCK_PI)
    #define MOCK_EARTH_MU_KM3_S2 398600.8

    // Initial orbit (the Simulink asbCubeSat harness's: a = 7021 km, e = 1e-5, i = 60 deg, RAAN = 137 deg,
    // argp = 349.1886 deg), but at 2026-11-01 00:00 UTC, since set_utc() rejects anything before 2026
    #define MOCK_SEMI_MAJOR_AXIS_KM 7021.0
    #define MOCK_EPOCH_JD 2461345.5

    // Initial tumble, in deg/s about [1, -1, 1] / sqrt(3), so B-dot has something to detumble before pointing
    #define MOCK_INITIAL_RATE_DEG_S 5.0
    #define MOCK_SEED 1u

    // How many plant ticks (PLANT_DT each) between summary logs
    #define MOCK_LOG_PERIOD_TICKS 10

// The plant's truth state; only touched by the ADCS task
static plant_t plant;
static plant_body_inputs_t inputs;
static bool plant_ready = false;

// Only read when the ADCS task executes the set TLE command, a tick after it's enqueued. Uplinks are a day apart in
// simulated time, so it's never overwritten before then.
static adcs_tle_t uplinked_tle;

static uint32_t tick = 0;
static uint32_t uplinks = 0;
static uint32_t pointing_ticks = 0;

/* ---------- LOGGING HELPERS ---------- */

// SEGGER_RTT_printf has no %f, so values are logged as fixed point: "%s%ld.%03lu" with these three arguments
    #define FIX3(x) fix_sign(x), fix_whole(x), fix_milli(x)
    #define FIX3_FMT "%s%ld.%03lu"

static const char *fix_sign(const double x) {
    return (x < 0.0) ? "-" : "";
}

static long fix_whole(const double x) {
    return isfinite(x) ? (long)fabs(x) : 0;
}

static unsigned long fix_milli(const double x) {
    if (!isfinite(x)) {
        return 0;
    }
    const double a = fabs(x);
    return (unsigned long)((a - floor(a)) * 1000.0) % 1000u;
}

/* ---------- SCORING ---------- */

static double angle_between_deg(const double *const a, const double *const b) {
    const double c = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) /
        (sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]) * sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]));
    return acos(c > 1.0 ? 1.0 : (c < -1.0 ? -1.0 : c)) * MOCK_RAD_TO_DEG;
}

// Angle between the true body +z axis and nadir, which is what body() points
static double nadir_error_deg(const plant_truth_t *const truth) {
    const double *const q = truth->q_body_to_eci;
    const double z_eci[3] = {2.0 * (q[1] * q[3] + q[0] * q[2]), 2.0 * (q[2] * q[3] - q[0] * q[1]),
                             q[0] * q[0] - q[1] * q[1] - q[2] * q[2] + q[3] * q[3]};
    const double nadir[3] = {-truth->r_eci_m[0], -truth->r_eci_m[1], -truth->r_eci_m[2]};
    return angle_between_deg(z_eci, nadir);
}

// Angle between body()'s attitude estimate and the truth, or NAN if it has no estimate
static double estimate_error_deg(const plant_truth_t *const truth) {
    float q_est[4];
    if (!body_get_attitude(q_est)) {
        return NAN;
    }
    const double *const q = truth->q_body_to_eci;
    double d = fabs(q_est[0] * q[0] + q_est[1] * q[1] + q_est[2] * q[2] + q_est[3] * q[3]) /
        sqrt(q_est[0] * q_est[0] + q_est[1] * q_est[1] + q_est[2] * q_est[2] + q_est[3] * q_est[3]);
    d = d > 1.0 ? 1.0 : d;
    return 2.0 * acos(d) * MOCK_RAD_TO_DEG;
}

// Distance between body()'s position estimate and the truth in km, or NAN if it has no estimate
static double position_error_km(const plant_truth_t *const truth) {
    float r_est[3];
    if (!body_get_position(r_est)) {
        return NAN;
    }
    const double dr[3] = {r_est[0] - truth->r_eci_m[0], r_est[1] - truth->r_eci_m[1], r_est[2] - truth->r_eci_m[2]};
    return sqrt(dr[0] * dr[0] + dr[1] * dr[1] + dr[2] * dr[2]) / 1000.0;
}

/* ---------- PUBLIC FUNCTIONS ---------- */

void adcs_mock_init(void) {
    plant_config_t cfg;
    plant_default_config(&cfg);

    // Near-circular, so the osculating elements are close enough to SGP4 mean elements to start from
    const double a = MOCK_SEMI_MAJOR_AXIS_KM;
    cfg.tle_mean[0] = (float)(sqrt(MOCK_EARTH_MU_KM3_S2 / (a * a * a)) * 86400.0 / (2.0 * MOCK_PI));
    cfg.tle_mean[1] = 1e-5f;
    cfg.tle_mean[2] = (float)(60.0 / MOCK_RAD_TO_DEG);
    cfg.tle_mean[3] = (float)(137.0 / MOCK_RAD_TO_DEG);
    cfg.tle_mean[4] = (float)(349.1886 / MOCK_RAD_TO_DEG);
    cfg.tle_mean[5] = 0.0f;
    cfg.bstar = 0.0f;
    cfg.tle_epoch_jd = MOCK_EPOCH_JD;
    cfg.start_jd = MOCK_EPOCH_JD;

    const double q0[4] = {0.7071, 0.0, 0.7071, 0.0};
    memcpy(cfg.q0_body_to_eci, q0, sizeof(q0));
    for (size_t i = 0; i < 3; i++) {
        cfg.w0_body_rad_s[i] = MOCK_INITIAL_RATE_DEG_S / MOCK_RAD_TO_DEG * (i == 1 ? -1.0 : 1.0) / sqrt(3.0);
    }
    cfg.seed = MOCK_SEED;

    info("adcs mock: initializing plant (a = %d km, i = 60 deg, |w0| = %d deg/s, seed %u, worst case %d)\n", (int)MOCK_SEMI_MAJOR_AXIS_KM,
         (int)MOCK_INITIAL_RATE_DEG_S, (unsigned)MOCK_SEED, cfg.worst_case);
    if (plant_init(&plant, &cfg) != 0) {
        fatal("adcs mock: plant_init failed (bad initial TLE?)\n");
    }
    plant_ready = true;

    // UTC goes through the real command path, so the RTC-based clock is exercised too
    command_t cmd = get_adcs_set_utc_command((uint32_t)lround((MOCK_EPOCH_JD - 2440587.5) * 86400.0));
    exec_command_adcs(&cmd);
    if (cmd.result != SUCCESS) {
        warning("adcs mock: set UTC command failed: %d\n", cmd.result);
    }
    info("adcs mock: plant ready, the ADCS task now runs against simulated sensors and magnetorquers\n");
}

void adcs_mock_sense(float *const last_mag, float *const mag, float *const gyro, float *const photodiodes, float *const dt) {
    plant_sense(&plant, &inputs);

    memcpy(last_mag, inputs.last_magnetometer, sizeof(inputs.last_magnetometer));
    memcpy(mag, inputs.magnetometer, sizeof(inputs.magnetometer));
    memcpy(gyro, inputs.gyro, sizeof(inputs.gyro));
    memcpy(photodiodes, inputs.photodiodes, sizeof(inputs.photodiodes));
    // The plant integrates exactly PLANT_DT per tick however long the step really took
    *dt = inputs.dt;

    // A ground uplink reaches the ADCS task the same way a real one will: as a set TLE command
    if (inputs.has_posn_update) {
        memcpy(uplinked_tle.mean, inputs.posn_update, sizeof(uplinked_tle.mean));
        uplinked_tle.bstar = inputs.bstar;
        uplinked_tle.epoch_jd = inputs.tle_epoch_jd;
        uplinks++;

        command_t cmd = get_adcs_set_tle_command(&uplinked_tle);
        const status_t result = enqueue_command(&cmd);
        if (result == SUCCESS) {
            info("adcs mock: uplink %lu enqueued (n = " FIX3_FMT " rev/day, fit residual " FIX3_FMT " m)\n", (unsigned long)uplinks,
                 FIX3(uplinked_tle.mean[0]), FIX3(plant.tle_fit_resid_m));
        } else {
            warning("adcs mock: failed to enqueue uplink %lu: %d\n", (unsigned long)uplinks, result);
        }
    }
}

status_t adcs_mock_get_time(int *const unix_time, int *const jd_scalar, float *const jd_frac) {
    if (!plant_ready) {
        return ERROR_NOT_READY;
    }
    *unix_time = inputs.unix_time;
    *jd_scalar = inputs.jd_scalar;
    *jd_frac = inputs.jd_frac;
    return SUCCESS;
}

void adcs_mock_step(const float *const currents, const TickType_t step_ticks) {
    plant_step(&plant, currents);
    tick++;

    if (body_is_pointing()) {
        pointing_ticks++;
    }

    if (tick % MOCK_LOG_PERIOD_TICKS != 0) {
        return;
    }

    plant_truth_t truth;
    plant_get_truth(&plant, &truth);
    const double *const w = truth.w_body_rad_s;
    const double rate_deg_s = sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]) * MOCK_RAD_TO_DEG;

    info("adcs mock: t " FIX3_FMT " s | pointing %d (%lu%% of run) | |w| " FIX3_FMT " deg/s | nadir err " FIX3_FMT
         " deg | est err " FIX3_FMT " deg | pos err " FIX3_FMT " km | sunlit %d | resets %d\n",
         FIX3(truth.t_s), body_is_pointing(), (unsigned long)(100u * pointing_ticks / tick), FIX3(rate_deg_s),
         FIX3(nadir_error_deg(&truth)), FIX3(estimate_error_deg(&truth)), FIX3(position_error_km(&truth)), truth.sunlit,
         body_get_filter_resets());
    info("adcs mock: currents [" FIX3_FMT ", " FIX3_FMT ", " FIX3_FMT "] A | mag [%ld, %ld, %ld] nT | step %lu ms\n", FIX3(currents[0]),
         FIX3(currents[1]), FIX3(currents[2]), isfinite(inputs.magnetometer[0]) ? (long)(inputs.magnetometer[0] * 1e9f) : 0,
         isfinite(inputs.magnetometer[1]) ? (long)(inputs.magnetometer[1] * 1e9f) : 0,
         isfinite(inputs.magnetometer[2]) ? (long)(inputs.magnetometer[2] * 1e9f) : 0,
         (unsigned long)(step_ticks * 1000u / configTICK_RATE_HZ));
}

#endif // ADCS_MOCK
