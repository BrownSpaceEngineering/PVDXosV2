/**
 * rtc_driver.c
 *
 * Hardware driver for RTC timer used in ADCS algorithms
 *
 * Created: November 9, 2025
 * Modified: November 24, 2025
 * Authors: Alexander Thaep
 */

#include "adcs_task.h"
#include "rtc_driver.h"

static const void *rtc_hw;

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
    return SUCCESS;
}

/**
 * \fn get_rtc_count
 *
 * \brief Get RTC raw count, microseconds, and seconds from the hardware counter
 *
 * \param data pointer to rtc_data_t structure to fill
 *
 * \returns status_t SUCCESS if reading was successful
 */
status_t get_rtc_values(rtc_data_t *data) {
    if (!rtc_hw) {
        warning("Attempting to get RTC count before initializing RTC");
        return ERROR_NOT_READY;
     }
    data->rtc_count = hri_rtcmode0_get_COUNT_reg(rtc_hw, 4294967295UL);
    data->microseconds_count = (data->rtc_count / 32);
    data->seconds_count = data->rtc_count / 32768;
    return SUCCESS;
}