/**
 * rtc_driver.c
 *
 * Hardware driver for RTC timer used in ADCS algorithms
 *
 * Created: November 9, 2025
 * Authors: Alexander Thaep
 */
#include "adcs_task.h"
#include "rtc_driver.h"

static const void *rtc_hw;
static uint32_t rtc_count;
static uint32_t seconds_count;
static uint32_t microseconds_count;

/**
 * \fn init_rtc_hardware
 *
 * \brief Initialize RTC timer hardware
 *
 * \returns status_t SUCCESS if initialization was successful
 */
status_t init_rtc_hardware(void) {
    rtc_hw = (&TIMER_0.device)->hw;
	hri_rtcmode0_clear_CTRLA_ENABLE_bit(rtc_hw);
    hri_rtcmode0_clear_CTRLA_MATCHCLR_bit(rtc_hw);
	hri_rtcmode0_write_COUNT_reg(rtc_hw, 0);
	hri_rtcmode0_wait_for_sync(rtc_hw, RTC_MODE0_SYNCBUSY_COUNT);
	hri_rtcmode0_set_CTRLA_ENABLE_bit(rtc_hw);

    rtc_count = 0;
    seconds_count = 0;
    microseconds_count = 0;

    return SUCCESS;
}

/**
 * \fn get_rtc_count
 *
 * \brief Get RTC timer hardware counter
 *
 * \returns uint32_t of the counter register belonging to RTC mode 0
 */
uint32_t get_rtc_count(void) {
    if (!rtc_hw) {
        warning("Attempting to get RTC count before initializing RTC");
        return 0;
     }
    rtc_count = hri_rtcmode0_get_COUNT_reg(rtc_hw, 4294967295UL);
    return rtc_count;
}



uint32_t get_seconds(void) {
    if (!rtc_hw) {
        warning("Attempting to get RTC seconds before initializing RTC");
        return 0;
     }
    get_rtc_count();
    seconds_count = rtc_count / 32768;
    return seconds_count;
}

uint32_t get_microseconds(void) {
    if (!rtc_hw) {
        warning("Attempting to get RTC microseconds before initializing RTC");
        return 0;
     }
    get_rtc_count();
    microseconds_count = (rtc_count / 32);
    return microseconds_count;
}