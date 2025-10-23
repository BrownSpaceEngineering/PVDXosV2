/**
 * camera_hw.c
 *
 * Camera hardware abstraction layer implementation.
 * Provides low-level hardware interface for ArduCam communication.
 *
 * Created: January 24, 2025
 * Authors: PVDX Team
 */

#include "camera_hw.h"
#include "misc/rtc/rtc_driver.h"

// ============================================================================
// HARDWARE STATE VARIABLES
// ============================================================================

// SPI transaction structure for ArduCam communication
struct spi_xfer camera_hw_spi_xfer = {.rxbuf = NULL, .txbuf = NULL, .size = 0};

// Camera communication buffers
static uint8_t camera_hw_tx_buffer[256];
static uint8_t camera_hw_rx_buffer[256];

// Hardware initialization status
static bool camera_hw_initialized = false;

// ============================================================================
// HARDWARE INITIALIZATION
// ============================================================================

/**
 * \brief Initialize camera hardware
 * \return status_t SUCCESS if hardware initialization was successful
 */
status_t camera_hw_init(void) {
    if (camera_hw_initialized) {
        warning("camera_hw: Hardware already initialized\n");
        return SUCCESS;
    }
    
    debug("camera_hw: Initializing ArduCam hardware\n");
    
    // Initialize SPI communication
    // Note: SPI_0 is already initialized in system_init()
    
    // Configure CS pin
    gpio_set_pin_level(CAMERA_HW_SPI_CS_PIN, true);  // CS high (inactive)
    gpio_set_pin_direction(CAMERA_HW_SPI_CS_PIN, GPIO_DIRECTION_OUT);
    gpio_set_pin_function(CAMERA_HW_SPI_CS_PIN, GPIO_PIN_FUNCTION_OFF);
    
    // Wait for camera to be ready
    if (camera_hw_wait_ready(CAMERA_HW_INIT_TIMEOUT_MS) != SUCCESS) {
        error("camera_hw: Camera not responding during initialization\n");
        return ERROR_TIMEOUT;
    }
    
    // Verify communication by reading sensor ID
    uint16_t sensor_id;
    if (camera_hw_get_sensor_id(&sensor_id) != SUCCESS) {
        error("camera_hw: Failed to read sensor ID\n");
        return ERROR_TIMEOUT;
    }
    
    debug("camera_hw: Sensor ID: 0x%04X\n", sensor_id);
    
    camera_hw_initialized = true;
    info("camera_hw: Hardware initialization complete\n");
    
    return SUCCESS;
}

/**
 * \brief Deinitialize camera hardware
 * \return status_t SUCCESS if hardware deinitialization was successful
 */
status_t camera_hw_deinit(void) {
    if (!camera_hw_initialized) {
        return SUCCESS;
    }
    
    debug("camera_hw: Deinitializing hardware\n");
    
    // Stop any ongoing operations
    camera_hw_stop_capture();
    
    // Set CS high (inactive)
    gpio_set_pin_level(CAMERA_HW_SPI_CS_PIN, true);
    
    camera_hw_initialized = false;
    info("camera_hw: Hardware deinitialized\n");
    
    return SUCCESS;
}

// ============================================================================
// HARDWARE REGISTER OPERATIONS
// ============================================================================

/**
 * \brief Read a register from the camera sensor
 * \param reg_addr Register address to read
 * \param value Pointer to store the read value
 * \return status_t SUCCESS if read was successful
 */
status_t camera_hw_read_register(uint16_t reg_addr, uint8_t *value) {
    if (!value) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    uint8_t tx_data[4] = {
        CAMERA_HW_CMD_READ_REG,
        (reg_addr >> 8) & 0xFF,
        reg_addr & 0xFF,
        0x00  // Dummy byte
    };
    
    uint8_t rx_data[4];
    
    // Perform SPI transaction
    status_t result = camera_hw_spi_transaction(tx_data, rx_data, 4);
    if (result != SUCCESS) {
        return result;
    }
    
    *value = rx_data[3];  // Data is in the last byte
    return SUCCESS;
}

/**
 * \brief Write a value to a camera sensor register
 * \param reg_addr Register address to write
 * \param value Value to write
 * \return status_t SUCCESS if write was successful
 */
status_t camera_hw_write_register(uint16_t reg_addr, uint8_t value) {
    uint8_t tx_data[4] = {
        CAMERA_HW_CMD_WRITE_REG,
        (reg_addr >> 8) & 0xFF,
        reg_addr & 0xFF,
        value
    };
    
    return camera_hw_spi_transaction(tx_data, NULL, 4);
}

// ============================================================================
// HARDWARE COMMUNICATION
// ============================================================================

/**
 * \brief Perform SPI transaction with camera
 * \param tx_data Pointer to data to transmit
 * \param rx_data Pointer to buffer for received data
 * \param length Number of bytes to transfer
 * \return status_t SUCCESS if SPI transaction was successful
 */
status_t camera_hw_spi_transaction(const uint8_t *tx_data, uint8_t *rx_data, uint16_t length) {
    if (!tx_data || length == 0) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    if (length > sizeof(camera_hw_tx_buffer)) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    // Copy data to internal buffers
    memcpy(camera_hw_tx_buffer, tx_data, length);
    
    // Set up SPI transaction
    camera_hw_spi_xfer.txbuf = camera_hw_tx_buffer;
    camera_hw_spi_xfer.rxbuf = rx_data ? camera_hw_rx_buffer : NULL;
    camera_hw_spi_xfer.size = length;
    
    // CS low (active)
    gpio_set_pin_level(CAMERA_HW_SPI_CS_PIN, false);
    
    // Perform SPI transaction
    status_t result = spi_m_sync_transfer(&SPI_0, &camera_hw_spi_xfer);
    
    // CS high (inactive)
    gpio_set_pin_level(CAMERA_HW_SPI_CS_PIN, true);
    
    if (result != SUCCESS) {
        warning("camera_hw: SPI transaction failed\n");
        return ERROR_TIMEOUT;
    }
    
    // Copy received data if requested
    if (rx_data) {
        memcpy(rx_data, camera_hw_rx_buffer, length);
    }
    
    return SUCCESS;
}

/**
 * \brief Send command to camera
 * \param command Command to send
 * \param param Optional parameter for command
 * \return status_t SUCCESS if command was sent successfully
 */
status_t camera_hw_send_command(uint8_t command, uint8_t param) {
    uint8_t cmd_data[2] = {command, param};
    
    status_t result = camera_hw_spi_transaction(cmd_data, NULL, 2);
    if (result != SUCCESS) {
        warning("camera_hw: Failed to send command 0x%02X with param 0x%02X\n", command, param);
        return result;
    }
    
    return SUCCESS;
}

/**
 * \brief Wait for camera to be ready
 * \param timeout_ms Timeout in milliseconds
 * \return status_t SUCCESS if camera is ready within timeout
 */
status_t camera_hw_wait_ready(uint32_t timeout_ms) {
    uint32_t start_time = rtc_get_seconds();
    uint32_t timeout_seconds = timeout_ms / 1000;
    
    while ((rtc_get_seconds() - start_time) < timeout_seconds) {
        // Check camera status by reading a register
        uint8_t status;
        status_t result = camera_hw_read_register(CAMERA_HW_REG_SENSOR_ID, &status);
        if (result == SUCCESS) {
            return SUCCESS;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    return ERROR_TIMEOUT;
}

// ============================================================================
// HARDWARE CAPTURE OPERATIONS
// ============================================================================

/**
 * \brief Start image capture
 * \return status_t SUCCESS if capture start was successful
 */
status_t camera_hw_start_capture(void) {
    if (!camera_hw_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    debug("camera_hw: Starting image capture\n");
    
    // Flush FIFO first
    camera_hw_flush_fifo();
    
    // Send capture command
    return camera_hw_send_command(CAMERA_HW_CMD_SINGLE_CAPTURE, 0);
}

/**
 * \brief Stop current capture operation
 * \return status_t SUCCESS if stop was successful
 */
status_t camera_hw_stop_capture(void) {
    if (!camera_hw_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    debug("camera_hw: Stopping capture\n");
    
    // Send stop capture command
    return camera_hw_send_command(CAMERA_HW_CMD_STOP_CAPTURE, 0);
}

/**
 * \brief Flush camera FIFO buffer
 * \return status_t SUCCESS if flush was successful
 */
status_t camera_hw_flush_fifo(void) {
    if (!camera_hw_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    debug("camera_hw: Flushing FIFO\n");
    
    // Send flush FIFO command
    return camera_hw_send_command(CAMERA_HW_CMD_FLUSH_FIFO, 0);
}

/**
 * \brief Get captured image data
 * \param image_buffer Pointer to image buffer to fill
 * \param timeout_ms Timeout in milliseconds
 * \return status_t SUCCESS if image was captured successfully
 */
status_t camera_hw_get_captured_image(camera_image_t *image_buffer, uint32_t timeout_ms) {
    if (!image_buffer) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    uint32_t start_time = rtc_get_seconds();
    uint32_t timeout_seconds = timeout_ms / 1000;
    
    debug("camera_hw: Waiting for capture completion\n");
    
    // Wait for capture to complete
    while ((rtc_get_seconds() - start_time) < timeout_seconds) {
        // TODO: Implement capture completion detection
        // This would typically involve checking a status register or interrupt
        
        // For now, use a simple delay
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Simulate successful capture after delay
        if ((rtc_get_seconds() - start_time) > 1) {  // 1 second delay
            break;
        }
    }
    
    // TODO: Implement actual image data reading from FIFO
    // This would involve:
    // 1. Reading FIFO length
    // 2. Reading image data in chunks
    // 3. Storing in image buffer
    
    // For now, simulate image data
    image_buffer->size = 1024;  // Simulated image size
    image_buffer->valid = true;
    
    debug("camera_hw: Image capture completed\n");
    return SUCCESS;
}

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================

/**
 * \brief Configure camera exposure
 * \param exposure Exposure value (0-255)
 * \return status_t SUCCESS if exposure was set successfully
 */
status_t camera_hw_set_exposure(uint8_t exposure) {
    if (!camera_hw_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    // Convert 8-bit exposure to 12-bit for camera registers
    uint16_t exposure_12bit = (exposure * 4095) / 255;
    
    uint8_t exposure_h = (exposure_12bit >> 8) & 0xFF;
    uint8_t exposure_m = (exposure_12bit >> 4) & 0xFF;
    uint8_t exposure_l = exposure_12bit & 0x0F;
    
    ret_err_status(camera_hw_write_register(CAMERA_HW_REG_EXPOSURE_H, exposure_h), 
                   "camera_hw: Failed to set exposure high byte");
    ret_err_status(camera_hw_write_register(CAMERA_HW_REG_EXPOSURE_M, exposure_m), 
                   "camera_hw: Failed to set exposure mid byte");
    ret_err_status(camera_hw_write_register(CAMERA_HW_REG_EXPOSURE_L, exposure_l), 
                   "camera_hw: Failed to set exposure low byte");
    
    debug("camera_hw: Exposure set to %d (0x%03X)\n", exposure, exposure_12bit);
    
    return SUCCESS;
}

/**
 * \brief Configure camera brightness
 * \param brightness Brightness value (0-255)
 * \return status_t SUCCESS if brightness was set successfully
 */
status_t camera_hw_set_brightness(uint8_t brightness) {
    if (!camera_hw_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    debug("camera_hw: Setting brightness to %d\n", brightness);
    
    // TODO: Implement brightness setting
    // This would involve writing to the appropriate camera register
    
    return SUCCESS;
}

/**
 * \brief Configure camera contrast
 * \param contrast Contrast value (0-255)
 * \return status_t SUCCESS if contrast was set successfully
 */
status_t camera_hw_set_contrast(uint8_t contrast) {
    if (!camera_hw_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    debug("camera_hw: Setting contrast to %d\n", contrast);
    
    // TODO: Implement contrast setting
    // This would involve writing to the appropriate camera register
    
    return SUCCESS;
}

/**
 * \brief Configure camera image format
 * \param format Image format to set
 * \return status_t SUCCESS if format was set successfully
 */
status_t camera_hw_set_format(camera_format_t format) {
    if (!camera_hw_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    debug("camera_hw: Setting format to %d\n", format);
    
    // TODO: Implement format setting
    // This would involve writing to the appropriate camera register
    
    return SUCCESS;
}

/**
 * \brief Configure camera resolution
 * \param width Image width
 * \param height Image height
 * \return status_t SUCCESS if resolution was set successfully
 */
status_t camera_hw_set_resolution(uint16_t width, uint16_t height) {
    if (!camera_hw_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    debug("camera_hw: Setting resolution to %dx%d\n", width, height);
    
    // TODO: Implement resolution setting
    // This would involve writing to the appropriate camera registers
    
    return SUCCESS;
}

// ============================================================================
// HARDWARE STATUS AND DIAGNOSTICS
// ============================================================================

/**
 * \brief Get camera sensor ID
 * \param sensor_id Pointer to store sensor ID
 * \return status_t SUCCESS if sensor ID was read successfully
 */
status_t camera_hw_get_sensor_id(uint16_t *sensor_id) {
    if (!sensor_id) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    uint8_t id_high, id_low;
    
    status_t result = camera_hw_read_register(CAMERA_HW_REG_SENSOR_ID, &id_high);
    if (result != SUCCESS) {
        return result;
    }
    
    result = camera_hw_read_register(CAMERA_HW_REG_SENSOR_ID + 1, &id_low);
    if (result != SUCCESS) {
        return result;
    }
    
    *sensor_id = (id_high << 8) | id_low;
    return SUCCESS;
}

/**
 * \brief Get camera chip ID
 * \param chip_id Pointer to store chip ID
 * \return status_t SUCCESS if chip ID was read successfully
 */
status_t camera_hw_get_chip_id(uint16_t *chip_id) {
    if (!chip_id) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    uint8_t id_high, id_low;
    
    status_t result = camera_hw_read_register(CAMERA_HW_REG_CHIP_ID, &id_high);
    if (result != SUCCESS) {
        return result;
    }
    
    result = camera_hw_read_register(CAMERA_HW_REG_CHIP_ID + 1, &id_low);
    if (result != SUCCESS) {
        return result;
    }
    
    *chip_id = (id_high << 8) | id_low;
    return SUCCESS;
}

/**
 * \brief Check if camera hardware is responding
 * \return status_t SUCCESS if camera is responding
 */
status_t camera_hw_check_communication(void) {
    if (!camera_hw_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    uint16_t sensor_id;
    status_t result = camera_hw_get_sensor_id(&sensor_id);
    
    if (result == SUCCESS) {
        debug("camera_hw: Communication check passed (Sensor ID: 0x%04X)\n", sensor_id);
    } else {
        warning("camera_hw: Communication check failed\n");
    }
    
    return result;
}
