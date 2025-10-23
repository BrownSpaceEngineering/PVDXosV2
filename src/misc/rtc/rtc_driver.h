/**
 * rtc_driver.h
 *
 * Real-Time Clock (RTC) driver for SAMD51 with 32-bit counter mode.
 * Provides 1-second incrementation for camera timestamp functionality.
 *
 * Created: January 24, 2025
 * Authors: PVDX Team
 */

#ifndef RTC_DRIVER_H
#define RTC_DRIVER_H

#include "atmel_start.h"
#include "globals.h"
#include "logging.h"

// RTC Configuration Constants
#define RTC_CLOCK_SOURCE_HZ 32768UL        // 32.768kHz oscillator
#define RTC_PRESCALER_DIV 32768UL           // Divide by 32768 to get 1Hz (1 second)
#define RTC_MAX_COUNT 0xFFFFFFFFUL          // 32-bit maximum value
#define RTC_ROLLOVER_SECONDS 4294967296UL   // Seconds before rollover (136 years)

// RTC Mode Configuration
#define RTC_MODE_32BIT_COUNTER 0            // MODE0: 32-bit counter
#define RTC_PRESCALER_VALUE 0xB             // DIV1024 prescaler (32768/1024 = 32Hz)
#define RTC_ADDITIONAL_DIV 32               // Additional software division for 1Hz

// Function declarations

/**
 * \brief Initialize RTC in 32-bit counter mode with 1-second increments
 * \return status_t SUCCESS if initialization was successful
 */
status_t rtc_init(void);

/**
 * \brief Get current RTC counter value in seconds
 * \return uint32_t Current RTC counter value (seconds since initialization)
 */
uint32_t rtc_get_seconds(void);

/**
 * \brief Set RTC counter value
 * \param seconds Value to set the RTC counter to
 * \return status_t SUCCESS if set was successful
 */
status_t rtc_set_seconds(uint32_t seconds);

/**
 * \brief Reset RTC counter to zero
 * \return status_t SUCCESS if reset was successful
 */
status_t rtc_reset(void);

/**
 * \brief Get RTC timestamp for camera images
 * \return uint32_t Current timestamp in seconds
 */
uint32_t rtc_get_timestamp(void);

/**
 * \brief Check if RTC is running
 * \return bool true if RTC is enabled and running
 */
bool rtc_is_running(void);

/**
 * \brief Get RTC status information
 * \param seconds Pointer to store current seconds value
 * \param running Pointer to store running status
 * \return status_t SUCCESS if status was retrieved successfully
 */
status_t rtc_get_status(uint32_t *seconds, bool *running);

/**
 * \brief Test RTC functionality
 * \return status_t SUCCESS if test passed
 */
status_t rtc_test(void);

#endif // RTC_DRIVER_H
