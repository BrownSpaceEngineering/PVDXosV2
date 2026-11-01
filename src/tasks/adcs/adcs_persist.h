#ifndef ADCS_PERSIST_H
#define ADCS_PERSIST_H

// Includes
#include <stdbool.h>
#include <stdint.h>

#include "globals.h"

/**
 * \fn adcs_persist_init
 *
 * \brief Brings up MRAM so that ADCS state can survive a reboot. If too few chips are working, the other
 *      `adcs_persist_*` functions do nothing and report that there is no saved state.
 *
 * \returns bool, whether a majority of the MRAM chips are working
 *
 * \warning blocks for at least 50 ms, so it must be called from a task
 */
bool adcs_persist_init(void);

/**
 * \fn adcs_persist_save_tle
 *
 * \brief Saves a TLE to MRAM
 *
 * \param tle the TLE to save
 */
void adcs_persist_save_tle(const adcs_tle_t *const tle);

/**
 * \fn adcs_persist_load_tle
 *
 * \brief Loads the last saved TLE from MRAM
 *
 * \param tle the TLE to fill
 *
 * \returns bool, whether a saved TLE was found and passed its integrity check
 */
bool adcs_persist_load_tle(adcs_tle_t *const tle);

/**
 * \fn adcs_persist_save_utc
 *
 * \brief Saves the current UTC to MRAM
 *
 * \param unix_time the current UTC, as seconds since the Unix epoch
 */
void adcs_persist_save_utc(const uint32_t unix_time);

/**
 * \fn adcs_persist_load_utc
 *
 * \brief Loads the last saved UTC from MRAM
 *
 * \param unix_time the UTC to fill, as seconds since the Unix epoch
 *
 * \returns bool, whether a saved UTC was found and passed its integrity check
 */
bool adcs_persist_load_utc(uint32_t *const unix_time);

#endif // ADCS_PERSIST_H
