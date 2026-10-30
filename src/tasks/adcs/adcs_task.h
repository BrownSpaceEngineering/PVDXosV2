#ifndef ADCS_H
#define ADCS_H

// Includes
#include <stddef.h>

#include "globals.h"
#include "telemetry/telemetry.h"

#define ADCS_ASCII_ART                                                                                                                     \
    "    _    ____   ____ ____     \n"                                                                                                     \
    "   / \\  |  _ \\ / ___/ ___|  \n"                                                                                                     \
    "  / _ \\ | | | | |   \\___ \\ \n"                                                                                                     \
    " / ___ \\| |_| | |___ ___) |  \n"                                                                                                     \
    "/_/   \\_\\____/ \\____|____/ \n"

// Constants
// The deepest path needs at least 5344 bytes at -Os (static analysis with -fcallgraph-info=su: main_adcs > body > iterate >
// ... > cholesky), not counting libm and soft-double helpers, or an interrupt's stacked FPU context.
// TODO(blocked on hardware run): trim using the "min free stack" debug log during pointing
#define ADCS_TASK_STACK_SIZE 2048 // Size of the stack in words (multiply by 4 to get bytes)

// body() counts calls, not seconds, for its BODY_MEASUREMENT_UPDATE_PERIOD_TICKS cycle, so this sets how long that cycle
// takes in real time
// TODO: retune the B-dot and PD gains, since the coils are now off for part of each step
#define ADCS_LOOP_PERIOD_MS 100 // How long the coils are driven between steps; must stay well below the watchdog timeout
// TODO(blocked on hardware run): measure the coil current's decay time and set this to match
#define ADCS_MTQ_SETTLE_MS 10 // How long the coils are off before the magnetometer starts measuring

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t adcs_task_stack[ADCS_TASK_STACK_SIZE];
    uint8_t adcs_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t adcs_task_queue;
    StaticTask_t adcs_task_tcb;
} adcs_task_memory_t;

// Global memory and configuration
extern adcs_task_memory_t adcs_mem;

// A TLE, as given to body()
struct adcs_tle {
    float mean[6];   // mean elements: [n (rev/day), e, i, RAAN, argp, M (rad)]
    float bstar;     // B* drag term
    double epoch_jd; // the TLE's real UTC epoch, as a Julian date (a float JD only resolves to ~0.25 day)
};

/* ---------- TASK ---------- */

/**
 * \fn init_adcs
 *
 * \brief Initialises the ADCS command queue and the hardware that is safe to bring up before the scheduler starts,
 *      before `init_task_pointer()`.
 *
 * \returns QueueHandle_t, a handle to the created queue
 *
 * \see `init_task_pointer()` for usage of functions of the type `init_<TASK>()`
 */
QueueHandle_t init_adcs(void);

/**
 * \fn main_adcs
 *
 * \param pvParameters a void pointer to the parameters required by ADCS functions; not currently set by config
 *
 * \warning should never return
 */
void main_adcs(void *pvParameters);

/* ---------- COMMANDS ---------- */

// TODO(blocked on uplink dispatcher): send these for TEL_CMD_SET_ADCS_OPMODE, _SET_UNIX_TIME and _ADCS_UPDATE_PARAMETERS
// TODO(blocked on uplink format): tel_adcs_parameters_t needs B* before it can become an adcs_tle_t

/**
 * \fn get_adcs_do_point_command
 *
 * \brief Creates a command to run `body()` whenever possible, falling back to B-dot whenever it is not pointing.
 *      This is the mode ADCS starts in.
 *
 * \warning any filter state left over from before a detumble-only period is reused, not reset; `body()`'s own
 *      failure checks are what catch it if it has gone stale
 *
 * \returns command_t command structure
 */
command_t get_adcs_do_point_command(void);

/**
 * \fn get_adcs_detumble_only_command
 *
 * \brief Creates a command to run only B-dot, without calling `body()` at all, until told otherwise
 *
 * \returns command_t command structure
 */
command_t get_adcs_detumble_only_command(void);

/**
 * \fn exec_command_adcs
 *
 * \brief Executes function corresponding to the command
 *
 * \param p_cmd a pointer to a command forwarded to the ADCS task
 */
void exec_command_adcs(command_t *const p_cmd);

/**
 * \fn get_adcs_set_tle_command
 *
 * \brief Creates a command to hand a new TLE to `body()`, which saves it to MRAM once `body()` has accepted it
 *
 * \param tle the TLE. Only read when the ADCS task executes the command, so it must outlive the command (e.g. be static)
 *
 * \returns command_t command structure
 */
command_t get_adcs_set_tle_command(const adcs_tle_t *const tle);

/**
 * \fn get_adcs_set_utc_command
 *
 * \brief Creates a command to set UTC, which the RTC keeps track of from then on, and to save it to MRAM
 *
 * \param unix_time the current UTC, as seconds since the Unix epoch
 *
 * \returns command_t command structure
 */
command_t get_adcs_set_utc_command(const uint32_t unix_time);

/* ---------- CONTROL ---------- */

/**
 * \fn adcs_compute_currents
 *
 * \brief Runs one ADCS control step in the current mode
 *
 * \param last_mag the previous step's magnetometer reading in Tesla (3 floats)
 * \param mag this step's magnetometer reading in Tesla (3 floats)
 * \param gyro this step's angular rate in rad/s (3 floats)
 * \param photodiodes this step's photodiode readings (NUM_DIODES floats)
 * \param dt time since the last step, in seconds
 * \param currents buffer of 3 floats to fill with magnetorquer currents in Amps; zero if the magnetometer is unusable
 *      and `body()` is not pointing
 */
void adcs_compute_currents(const float *const last_mag, const float *const mag, const float *const gyro, const float *const photodiodes,
                           const float dt, float *const currents);

/* ---------- SENSORS ---------- */

/**
 * \fn adcs_read_sensors
 *
 * \brief Reads all three ADCS sensors, and records whether any failed its device check for `adcs_update_state_inputs()`
 *
 * \param mag_tesla see `adcs_read_magnetometer()`
 * \param gyro_rad_s see `adcs_read_gyro()`
 * \param photodiodes see `adcs_read_photodiodes()`
 *
 * \warning the magnetorquers must be off, since this takes a magnetometer measurement
 */
void adcs_read_sensors(float *const mag_tesla, float *const gyro_rad_s, float *const photodiodes);

/**
 * \fn adcs_read_magnetometer
 *
 * \brief Takes a single magnetometer measurement, in the body frame and in Tesla, as `body()` expects
 *
 * \param mag_tesla buffer of 3 floats to fill; set to NAN on failure so `body()` safely bails
 *
 * \returns status_t SUCCESS if the reading is valid
 *
 * \warning the magnetorquers must be off, and blocks for at least the measurement time
 */
status_t adcs_read_magnetometer(float *const mag_tesla);

/**
 * \fn adcs_read_gyro
 *
 * \brief Reads the body-frame angular rate in rad/s, as `body()` expects
 *
 * \param gyro_rad_s buffer of 3 floats to fill; set to NAN on failure so `body()` safely bails
 *
 * \returns status_t SUCCESS if the reading is valid
 */
status_t adcs_read_gyro(float *const gyro_rad_s);

/**
 * \fn adcs_read_photodiodes
 *
 * \brief Reads the photodiodes in the adcs-c `PHOTODIODES` order, in volts
 *
 * \param photodiodes buffer of NUM_DIODES floats to fill; set to 0 (dark) on failure
 *
 * \returns status_t SUCCESS if the readings are valid
 */
status_t adcs_read_photodiodes(float *const photodiodes);

/**
 * \fn adcs_get_tle_update
 *
 * \brief Gets the TLE to hand to `body()` if there is a new one, which stays pending until `body()` accepts it or
 *      has rejected it too many times
 *
 * \returns pointer to 6 TLE mean elements [n (rev/day), e, i, RAAN, argp, M (rad)], or NULL to reuse the last TLE
 */
const float *adcs_get_tle_update(void);

/**
 * \fn adcs_get_time
 *
 * \brief Gets the current UTC time as both Unix time and a split Julian date
 *
 * \param unix_time seconds since the Unix epoch
 * \param jd_scalar integer part of the Julian date
 * \param jd_frac fractional part of the Julian date, in [0, 1)
 *
 * \returns status_t SUCCESS if the time was read, or ERROR_NOT_READY if UTC isn't known yet
 */
status_t adcs_get_time(int *const unix_time, int *const jd_scalar, float *const jd_frac);

/**
 * \fn adcs_restore_state
 *
 * \brief Brings up MRAM and restores the saved UTC, then the saved TLE. The TLE is skipped if there is no saved UTC,
 *      or if its epoch is too far from that UTC. The restored UTC is behind by however long the reboot took (plus
 *      up to one save period) until the next time sync.
 *
 * \warning must be called after `body_reset()`, which would otherwise discard the restored TLE
 */
void adcs_restore_state(void);

/**
 * \fn adcs_save_state
 *
 * \brief Saves UTC to MRAM once per save period. Should be called every step. A TLE is saved when `body()` accepts it.
 */
void adcs_save_state(void);

/**
 * \fn adcs_drive_magnetorquers
 *
 * \brief Commands the magnetorquers with the currents computed by `adcs_compute_currents()`
 *
 * \warning must not be called while the magnetometer is measuring, since the coils' field would corrupt the reading.
 *      The main loop stops the coils before each measurement and only calls this once it's done.
 *
 * \param currents 3 magnetorquer currents in Amps (x, y, z); a non-finite current turns that coil off
 */
void adcs_drive_magnetorquers(const float *const currents);

/**
 * \fn adcs_publish_telemetry
 *
 * \brief Publishes the current ADCS estimate for telemetry
 */
void adcs_publish_telemetry(void);

// TODO(blocked on downlink assembler): call this when building tel_downlink_t
/**
 * \fn adcs_get_telemetry
 *
 * \brief Copies out the latest ADCS telemetry. Safe to call from any task.
 *
 * \param telemetry the telemetry to fill
 */
void adcs_get_telemetry(tel_adcs_data_t *const telemetry);

/**
 * \fn adcs_update_state_inputs
 *
 * \brief Tells the task manager if the ADCS state machine inputs (pointing, sensor fault) have changed
 */
void adcs_update_state_inputs(void);

#endif // ADCS_H
