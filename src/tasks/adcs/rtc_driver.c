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

/**
 * \fn init_rtc_hardware
 *
 * \brief Initialize RTC timer hardware
 *
 * \returns status_t SUCCESS if initialization was successful
 */
status_t init_rtc_hardware(void) {
    timer_init(&TIMER_0);
    timer_start(&TIMER_0);

    uint32_t c1 = hri_rtcmode0_get_COUNT_reg(&TIMER_0.device, 4294967295UL);
    delay_ms(1000);
    uint32_t c2 = hri_rtcmode0_get_COUNT_reg(&TIMER_0.device, 4294967295UL);
    
    return SUCCESS;
}