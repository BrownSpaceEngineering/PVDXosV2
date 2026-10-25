#ifndef ADCS_H
#define ADCS_H

// Includes
#include <stddef.h>

#include "globals.h"

// TODO: cool ascii art
#define ADCS_ASCII_ART                                                                                                                     \
    "    _    ____   ____ ____     \n"                                                                                                     \
    "   / \\  |  _ \\ / ___/ ___|  \n"                                                                                                     \
    "  / _ \\ | | | | |   \\___ \\ \n"                                                                                                     \
    " / ___ \\| |_| | |___ ___) |  \n"                                                                                                     \
    "/_/   \\_\\____/ \\____|____/ \n"

// Constants
// TODO: profile stack usage of body() (UKF + SGP4 + WMM) on hardware and resize if needed
#define ADCS_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// BODY_MEASUREMENT_UPDATE_PERIOD_TICKS cycle, so this sets how long that cycle takes in real time
#define ADCS_LOOP_PERIOD_MS 100 // Period of one body() step; must stay well below the watchdog timeout

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t adcs_task_stack[ADCS_TASK_STACK_SIZE];
    StaticTask_t adcs_task_tcb;
} adcs_task_memory_t;

// Global memory and configuration
extern adcs_task_memory_t adcs_mem;

/* ---------- TASK ---------- */

/**
 * \fn init_adcs
 *
 * \brief Initialises ADCS hardware that is safe to bring up before the scheduler starts, before `init_task_pointer()`.
 *
 * \returns QueueHandle_t, always NULL since the ADCS task takes no commands
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

/* ---------- SENSORS ---------- */

/**
 * \fn adcs_read_magnetometer
 *
 * \brief Reads the body-frame magnetic field in Tesla, as `body()` expects
 *
 * \param mag_tesla buffer of 3 floats to fill; set to NAN on failure so `body()` safely bails
 *
 * \returns status_t SUCCESS if the reading is valid
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
 * \brief Reads the photodiodes in the adcs-c `PHOTODIODES` order
 *
 * \param photodiodes buffer of NUM_DIODES floats to fill; set to 0 (dark) on failure
 *
 * \returns status_t SUCCESS if the readings are valid
 */
status_t adcs_read_photodiodes(float *const photodiodes);

/**
 * \fn adcs_get_tle_update
 *
 * \brief Gets a newly uplinked TLE to hand to `body()`, if there is one
 *
 * \returns pointer to 6 TLE mean elements [n (rev/day), e, i, RAAN, argp, M (rad)], or NULLPTR to reuse the last TLE
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
 * \returns status_t SUCCESS if the time was read
 */
status_t adcs_get_time(int *const unix_time, int *const jd_scalar, float *const jd_frac);

/**
 * \fn adcs_drive_magnetorquers
 *
 * \brief Commands the magnetorquers with the currents computed by `body()`
 *
 * \note TODO: NEVER CALL THIS WHEN MAGNETOMETER TAKING READINGS
 *
 * \param currents 3 magnetorquer currents in Amps (x, y, z)
 */
void adcs_drive_magnetorquers(const float *const currents);

/**
 * \fn adcs_publish_telemetry
 *
 * \brief Publishes the current ADCS estimate for telemetry
 */
void adcs_publish_telemetry(void);

#endif // ADCS_H
