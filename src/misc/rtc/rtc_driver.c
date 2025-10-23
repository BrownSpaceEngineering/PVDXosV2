/**
 * rtc_driver.c
 *
 * Real-Time Clock (RTC) driver implementation for SAMD51 with 32-bit counter mode.
 * Provides 1-second incrementation for camera timestamp functionality.
 *
 * Created: January 24, 2025
 * Authors: PVDX Team
 */

#include "rtc_driver.h"
#include "driver_init.h"

// RTC state variables
static bool rtc_initialized = false;
static bool rtc_running = false;
static uint32_t rtc_software_counter = 0;
static uint32_t last_hardware_count = 0;

/**
 * \brief Initialize RTC in 32-bit counter mode with 1-second increments
 * \return status_t SUCCESS if initialization was successful
 */
status_t rtc_init(void) {
    if (rtc_initialized) {
        warning("rtc: RTC already initialized\n");
        return SUCCESS;
    }

    debug("rtc: Initializing RTC in 32-bit counter mode\n");

    // Configure RTC for MODE0 (32-bit counter)
    // Set mode to 32-bit counter
    hri_rtc_write_CTRLA_reg(RTC, RTC_MODE0_CTRLA_MODE_COUNT32);
    
    // Configure prescaler for 1-second increments
    // 32.768kHz / 1024 = 32Hz, then we'll use software division for 1Hz
    hri_rtc_write_CTRLA_reg(RTC, hri_rtc_read_CTRLA_reg(RTC) | 
                            RTC_MODE0_CTRLA_PRESCALER_DIV1024);
    
    // Enable RTC
    hri_rtc_set_CTRLA_ENABLE_bit(RTC);
    
    // Wait for enable to take effect
    while (hri_rtc_get_SYNCBUSY_reg(RTC, RTC_MODE0_SYNCBUSY_ENABLE)) {
        // Wait for synchronization
    }
    
    // Reset counter to zero
    hri_rtc_write_COUNT_reg(RTC, 0);
    
    // Wait for count write to complete
    while (hri_rtc_get_SYNCBUSY_reg(RTC, RTC_MODE0_SYNCBUSY_COUNT)) {
        // Wait for synchronization
    }
    
    // Initialize software variables
    rtc_software_counter = 0;
    last_hardware_count = 0;
    rtc_running = true;
    rtc_initialized = true;
    
    info("rtc: RTC initialized successfully in 32-bit counter mode\n");
    return SUCCESS;
}

/**
 * \brief Get current RTC counter value in seconds
 * \return uint32_t Current RTC counter value (seconds since initialization)
 */
uint32_t rtc_get_seconds(void) {
    if (!rtc_initialized || !rtc_running) {
        warning("rtc: RTC not initialized or not running\n");
        return 0;
    }
    
    // Read hardware counter (32Hz)
    uint32_t current_hw_count = hri_rtc_read_COUNT_reg(RTC);
    
    // Calculate seconds from hardware counter
    // Hardware runs at 32Hz, so divide by 32 to get seconds
    uint32_t hardware_seconds = current_hw_count / RTC_ADDITIONAL_DIV;
    
    // Handle rollover detection
    if (current_hw_count < last_hardware_count) {
        // Hardware counter rolled over, increment software counter
        rtc_software_counter += (RTC_MAX_COUNT / RTC_ADDITIONAL_DIV);
    }
    
    last_hardware_count = current_hw_count;
    
    // Return total seconds
    return rtc_software_counter + hardware_seconds;
}

/**
 * \brief Set RTC counter value
 * \param seconds Value to set the RTC counter to
 * \return status_t SUCCESS if set was successful
 */
status_t rtc_set_seconds(uint32_t seconds) {
    if (!rtc_initialized) {
        error("rtc: RTC not initialized\n");
        return ERROR_NOT_INITIALIZED;
    }
    
    // Disable RTC temporarily
    hri_rtc_clear_CTRLA_ENABLE_bit(RTC);
    
    // Wait for disable to take effect
    while (hri_rtc_get_SYNCBUSY_reg(RTC, RTC_MODE0_SYNCBUSY_ENABLE)) {
        // Wait for synchronization
    }
    
    // Set hardware counter to 0
    hri_rtc_write_COUNT_reg(RTC, 0);
    
    // Wait for count write to complete
    while (hri_rtc_get_SYNCBUSY_reg(RTC, RTC_MODE0_SYNCBUSY_COUNT)) {
        // Wait for synchronization
    }
    
    // Set software counter
    rtc_software_counter = seconds;
    last_hardware_count = 0;
    
    // Re-enable RTC
    hri_rtc_set_CTRLA_ENABLE_bit(RTC);
    
    // Wait for enable to take effect
    while (hri_rtc_get_SYNCBUSY_reg(RTC, RTC_MODE0_SYNCBUSY_ENABLE)) {
        // Wait for synchronization
    }
    
    debug("rtc: RTC counter set to %lu seconds\n", (unsigned long)seconds);
    return SUCCESS;
}

/**
 * \brief Reset RTC counter to zero
 * \return status_t SUCCESS if reset was successful
 */
status_t rtc_reset(void) {
    return rtc_set_seconds(0);
}

/**
 * \brief Get RTC timestamp for camera images
 * \return uint32_t Current timestamp in seconds
 */
uint32_t rtc_get_timestamp(void) {
    return rtc_get_seconds();
}

/**
 * \brief Check if RTC is running
 * \return bool true if RTC is enabled and running
 */
bool rtc_is_running(void) {
    return rtc_initialized && rtc_running && hri_rtc_get_CTRLA_ENABLE_bit(RTC);
}

/**
 * \brief Get RTC status information
 * \param seconds Pointer to store current seconds value
 * \param running Pointer to store running status
 * \return status_t SUCCESS if status was retrieved successfully
 */
status_t rtc_get_status(uint32_t *seconds, bool *running) {
    if (!seconds || !running) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    *seconds = rtc_get_seconds();
    *running = rtc_is_running();
    
    return SUCCESS;
}

/**
 * \brief Test RTC functionality
 * \return status_t SUCCESS if test passed
 */
status_t rtc_test(void) {
    if (!rtc_initialized) {
        error("rtc: RTC not initialized for testing\n");
        return ERROR_NOT_INITIALIZED;
    }
    
    info("rtc: Starting RTC functionality test\n");
    
    // Test 1: Check if RTC is running
    if (!rtc_is_running()) {
        error("rtc: RTC is not running\n");
        return ERROR_NOT_READY;
    }
    info("rtc: ✓ RTC is running\n");
    
    // Test 2: Get initial timestamp
    uint32_t initial_time = rtc_get_seconds();
    info("rtc: Initial time: %lu seconds\n", (unsigned long)initial_time);
    
    // Test 3: Wait for 2 seconds and check increment
    info("rtc: Waiting 2 seconds to test increment...\n");
    vTaskDelay(pdMS_TO_TICKS(2000));  // Wait 2 seconds
    
    uint32_t final_time = rtc_get_seconds();
    uint32_t elapsed = final_time - initial_time;
    
    info("rtc: Final time: %lu seconds\n", (unsigned long)final_time);
    info("rtc: Elapsed time: %lu seconds\n", (unsigned long)elapsed);
    
    // Test 4: Verify increment (allow for some tolerance)
    if (elapsed >= 1 && elapsed <= 3) {  // Allow 1-3 seconds tolerance
        info("rtc: ✓ Time increment test passed\n");
    } else {
        error("rtc: ✗ Time increment test failed (expected ~2, got %lu)\n", (unsigned long)elapsed);
        return ERROR_TIMEOUT;
    }
    
    // Test 5: Test set/reset functionality
    uint32_t test_value = 1000;
    if (rtc_set_seconds(test_value) != SUCCESS) {
        error("rtc: ✗ Failed to set RTC to test value\n");
        return ERROR_TIMEOUT;
    }
    
    uint32_t read_value = rtc_get_seconds();
    if (read_value == test_value) {
        info("rtc: ✓ Set/Get test passed\n");
    } else {
        error("rtc: ✗ Set/Get test failed (set %lu, got %lu)\n", (unsigned long)test_value, (unsigned long)read_value);
        return ERROR_TIMEOUT;
    }
    
    // Reset to current time
    rtc_set_seconds(initial_time + elapsed);
    
    info("rtc: ✓ All RTC tests passed successfully\n");
    return SUCCESS;
}
