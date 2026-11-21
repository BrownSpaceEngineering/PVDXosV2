#ifndef UTC_H
#define UTC_H

#include <stdint.h>

#include "globals.h"

// UTC, kept as an offset from the RTC's calibrated uptime. Safe to call from any task once the RTC is up.
// The ADCS task sets it from uplinked time syncs and saves/restores it across reboots (see adcs_persist.h).

/**
 * \fn utc_set
 *
 * \brief Sets the current UTC, which the RTC keeps track of from then on
 *
 * \param unix_time the current UTC, as seconds since the Unix epoch
 *
 * \returns status_t SUCCESS, ERROR_BAD_ARGS if the time is implausibly early, or the RTC's error if it can't be read
 */
status_t utc_set(const uint32_t unix_time);

/**
 * \fn utc_get
 *
 * \brief Gets the current UTC
 *
 * \param unix_time filled with seconds since the Unix epoch
 *
 * \returns status_t SUCCESS, ERROR_NOT_READY if UTC hasn't been set since boot, or the RTC's error if it can't be read
 */
status_t utc_get(uint32_t *const unix_time);

/**
 * \fn utc_to_jd
 *
 * \brief Converts Unix time to a Julian date, split so that a float fraction keeps sub-second resolution
 *
 * \param unix_time seconds since the Unix epoch
 * \param jd_scalar integer part of the Julian date
 * \param jd_frac fractional part of the Julian date, in [0, 1)
 */
void utc_to_jd(const uint32_t unix_time, int *const jd_scalar, float *const jd_frac);

#endif // UTC_H
