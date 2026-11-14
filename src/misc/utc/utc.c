/**
 * utc.c
 *
 * UTC, kept as an offset from the RTC's calibrated uptime, so that any task can timestamp with it
 *
 * Created: November 13, 2026
 * Authors: Zach Mahan
 */

#include "utc.h"

#include <FreeRTOS.h>
#include <task.h>

#include "drivers/rtc/rtc_driver.h"
#include "logging.h"

#define SECONDS_PER_DAY 86400
#define UNIX_EPOCH_JD_SCALAR 2440587 // The Unix epoch is JD 2440587.5
#define UNIX_EPOCH_JD_FRAC 0.5f

// A UTC earlier than this can't be real (2026-01-01T00:00:00Z)
#define UTC_MIN_VALID_UNIX_TIME 1767225600u

// Written by utc_set() and read by utc_get() from different tasks, so both are only touched in a critical section
static bool utc_valid = false;
static uint32_t utc_at_rtc_zero = 0; // UTC when the RTC read zero

// Fails only if the RTC can't be read. The RTC driver's seconds are calibrated against the UHF radio and don't wrap.
static status_t get_uptime_seconds(uint32_t *const uptime_s) {
    rtc_data_t rtc;
    const status_t result = get_rtc_values(&rtc);
    if (result != SUCCESS) {
        warning("utc: failed to read RTC\n");
        return result;
    }

    *uptime_s = rtc.seconds_count;
    return SUCCESS;
}

status_t utc_set(const uint32_t unix_time) {
    if (unix_time < UTC_MIN_VALID_UNIX_TIME) {
        warning("utc: rejected implausible UTC %lu\n", (unsigned long)unix_time);
        return ERROR_BAD_ARGS;
    }

    uint32_t uptime_s;
    const status_t result = get_uptime_seconds(&uptime_s);
    if (result != SUCCESS) {
        return result;
    }

    taskENTER_CRITICAL();
    utc_at_rtc_zero = unix_time - uptime_s;
    utc_valid = true;
    taskEXIT_CRITICAL();
    return SUCCESS;
}

status_t utc_get(uint32_t *const unix_time) {
    taskENTER_CRITICAL();
    const bool valid = utc_valid;
    const uint32_t offset = utc_at_rtc_zero;
    taskEXIT_CRITICAL();

    if (!valid) {
        return ERROR_NOT_READY;
    }

    uint32_t uptime_s;
    const status_t result = get_uptime_seconds(&uptime_s);
    if (result != SUCCESS) {
        return result;
    }

    *unix_time = offset + uptime_s;
    return SUCCESS;
}

void utc_to_jd(const uint32_t unix_time, int *const jd_scalar, float *const jd_frac) {
    *jd_scalar = UNIX_EPOCH_JD_SCALAR + (int)(unix_time / SECONDS_PER_DAY);
    *jd_frac = UNIX_EPOCH_JD_FRAC + (float)(unix_time % SECONDS_PER_DAY) / SECONDS_PER_DAY;
    if (*jd_frac >= 1.0f) {
        *jd_scalar += 1;
        *jd_frac -= 1.0f;
    }
}
