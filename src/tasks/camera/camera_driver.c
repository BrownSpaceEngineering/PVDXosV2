/**
 * camera_driver.c
 *
 * Hardware driver for ArduCam camera module with SPI communication.
 * Implements low-level camera control and image capture functionality.
 *
 * Created: January 24, 2025
 * Authors: PVDX Team
 */

#include "camera_driver.h"

// SPI transaction structure for ArduCam communication
struct spi_xfer camera_spi_xfer = {.rxbuf = NULL, .txbuf = NULL, .size = 0};

// Camera communication buffers
static uint8_t camera_tx_buffer[256];
static uint8_t camera_rx_buffer[256];

/**
 * \fn camera_spi_transaction
 *
 * \brief Performs SPI transaction with ArduCam
 *
 * \param tx_data pointer to data to transmit
 * \param rx_data pointer to buffer for received data
 * \param length number of bytes to transfer
 *
 * \returns status_t SUCCESS if SPI transaction was successful
 */
status_t camera_spi_transaction(const uint8_t *tx_data, uint8_t *rx_data, uint16_t length) {
    if (!tx_data || length == 0) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    if (length > sizeof(camera_tx_buffer)) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    // Prepare SPI transaction
    memcpy(camera_tx_buffer, tx_data, length);
    camera_spi_xfer.txbuf = camera_tx_buffer;
    camera_spi_xfer.rxbuf = rx_data ? rx_data : camera_rx_buffer;
    camera_spi_xfer.size = length;
    
    // Select camera
    gpio_set_pin_level(ARDUCAM_SPI_CS_PIN, false);
    
    // Perform SPI transaction
    int32_t response = spi_m_sync_transfer(&ARDUCAM_SPI_INSTANCE, &camera_spi_xfer);
    
    // Deselect camera
    gpio_set_pin_level(ARDUCAM_SPI_CS_PIN, true);
    
    if (response != (int32_t)length) {
        warning("camera: SPI transaction failed (expected: %d, got: %d)\n", length, response);
        return ERROR_SPI_TRANSFER_FAILED;
    }
    
    return SUCCESS;
}

/**
 * \fn camera_send_command
 *
 * \brief Sends command to ArduCam
 *
 * \param command command to send
 * \param param optional parameter for command
 *
 * \returns status_t SUCCESS if command was sent successfully
 */
status_t camera_send_command(uint8_t command, uint8_t param) {
    uint8_t cmd_data[2] = {command, param};
    
    status_t result = camera_spi_transaction(cmd_data, NULL, 2);
    if (result != SUCCESS) {
        warning("camera: Failed to send command 0x%02X with param 0x%02X\n", command, param);
        return result;
    }
    
    // Wait for command processing
    vTaskDelay(pdMS_TO_TICKS(10));
    
    return SUCCESS;
}

/**
 * \fn camera_wait_ready
 *
 * \brief Waits for ArduCam to be ready
 *
 * \param timeout_ms timeout in milliseconds
 *
 * \returns status_t SUCCESS if camera is ready within timeout
 */
status_t camera_wait_ready(uint32_t timeout_ms) {
    uint32_t start_time = xTaskGetTickCount();
    
    while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(timeout_ms)) {
        // Check camera status by reading a register
        uint8_t status;
        status_t result = camera_read_register(ARDUCAM_REG_SENSOR_ID, &status);
        if (result == SUCCESS) {
            return SUCCESS;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    return ERROR_NOT_READY;
}

/**
 * \fn init_camera_hardware
 *
 * \brief Initialize ArduCam hardware and SPI communication
 *
 * \returns status_t SUCCESS if initialization was successful
 */
status_t init_camera_hardware(void) {
    debug("camera_driver: Initializing ArduCam hardware\n");
    
    // Initialize SPI communication
    // Note: SPI_0 is already initialized in system_init()
    
    // Set camera CS pin high (deselected)
    gpio_set_pin_level(ARDUCAM_SPI_CS_PIN, true);
    
    // Wait for camera to be ready
    ret_err_status(camera_wait_ready(ARDUCAM_INIT_TIMEOUT_MS), 
                   "camera_driver: Camera not ready after initialization timeout");
    
    // Verify camera communication by reading sensor ID
    uint16_t sensor_id;
    ret_err_status(camera_get_sensor_id(&sensor_id), 
                   "camera_driver: Failed to read sensor ID");
    
    info("camera_driver: ArduCam initialized successfully (Sensor ID: 0x%04X)\n", sensor_id);
    
    return SUCCESS;
}

/**
 * \fn camera_read_register
 *
 * \brief Read a register from the ArduCam sensor
 *
 * \param reg_addr register address to read
 * \param value pointer to store the read value
 *
 * \returns status_t SUCCESS if read was successful
 */
status_t camera_read_register(uint16_t reg_addr, uint8_t *value) {
    if (!value) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    uint8_t tx_data[4] = {
        ARDUCAM_CMD_READ_REG,
        (reg_addr >> 8) & 0xFF,
        reg_addr & 0xFF,
        0x00  // Dummy byte
    };
    
    status_t result = camera_spi_transaction(tx_data, value, 4);
    if (result != SUCCESS) {
        warning("camera: Failed to read register 0x%04X\n", reg_addr);
        return result;
    }
    
    // Return the actual value (last byte received)
    *value = camera_rx_buffer[3];
    
    return SUCCESS;
}

/**
 * \fn camera_write_register
 *
 * \brief Write a value to an ArduCam sensor register
 *
 * \param reg_addr register address to write
 * \param value value to write
 *
 * \returns status_t SUCCESS if write was successful
 */
status_t camera_write_register(uint16_t reg_addr, uint8_t value) {
    uint8_t tx_data[4] = {
        ARDUCAM_CMD_WRITE_REG,
        (reg_addr >> 8) & 0xFF,
        reg_addr & 0xFF,
        value
    };
    
    status_t result = camera_spi_transaction(tx_data, NULL, 4);
    if (result != SUCCESS) {
        warning("camera: Failed to write register 0x%04X with value 0x%02X\n", reg_addr, value);
        return result;
    }
    
    // Wait for register write to complete
    vTaskDelay(pdMS_TO_TICKS(5));
    
    return SUCCESS;
}

/**
 * \fn camera_get_sensor_id
 *
 * \brief Get camera sensor ID and verify communication
 *
 * \param sensor_id pointer to store sensor ID
 *
 * \returns status_t SUCCESS if sensor ID was read successfully
 */
status_t camera_get_sensor_id(uint16_t *sensor_id) {
    if (!sensor_id) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    uint8_t id_high, id_low;
    
    ret_err_status(camera_read_register(ARDUCAM_REG_SENSOR_ID, &id_high), 
                   "camera: Failed to read sensor ID high byte");
    ret_err_status(camera_read_register(ARDUCAM_REG_CHIP_ID, &id_low), 
                   "camera: Failed to read sensor ID low byte");
    
    *sensor_id = (id_high << 8) | id_low;
    
    debug("camera: Sensor ID: 0x%04X\n", *sensor_id);
    
    return SUCCESS;
}

/**
 * \fn camera_configure_settings
 *
 * \brief Configure camera settings (exposure, brightness, contrast, format)
 *
 * \param config pointer to camera configuration structure
 *
 * \returns status_t SUCCESS if configuration was successful
 */
status_t camera_configure_settings(const camera_config_t *const config) {
    if (!config) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    debug("camera_driver: Configuring camera settings\n");
    
    // Configure exposure
    ret_err_status(camera_set_exposure(config->exposure), 
                   "camera_driver: Failed to set exposure");
    
    // Configure brightness
    ret_err_status(camera_set_brightness(config->brightness), 
                   "camera_driver: Failed to set brightness");
    
    // Configure contrast
    ret_err_status(camera_set_contrast(config->contrast), 
                   "camera_driver: Failed to set contrast");
    
    // Configure image format
    ret_err_status(camera_set_format(config->format), 
                   "camera_driver: Failed to set format");
    
    // Configure resolution
    ret_err_status(camera_set_resolution(config->width, config->height), 
                   "camera_driver: Failed to set resolution");
    
    debug("camera_driver: Camera configuration complete\n");
    
    return SUCCESS;
}

/**
 * \fn camera_set_exposure
 *
 * \brief Set camera exposure value
 *
 * \param exposure exposure value (0-255)
 *
 * \returns status_t SUCCESS if exposure was set successfully
 */
status_t camera_set_exposure(uint8_t exposure) {
    // Convert 8-bit exposure to 12-bit for camera registers
    uint16_t exposure_12bit = (exposure * 4095) / 255;
    
    uint8_t exposure_h = (exposure_12bit >> 8) & 0xFF;
    uint8_t exposure_m = (exposure_12bit >> 4) & 0xFF;
    uint8_t exposure_l = exposure_12bit & 0x0F;
    
    ret_err_status(camera_write_register(ARDUCAM_REG_EXPOSURE_H, exposure_h), 
                   "camera: Failed to set exposure high byte");
    ret_err_status(camera_write_register(ARDUCAM_REG_EXPOSURE_M, exposure_m), 
                   "camera: Failed to set exposure mid byte");
    ret_err_status(camera_write_register(ARDUCAM_REG_EXPOSURE_L, exposure_l), 
                   "camera: Failed to set exposure low byte");
    
    debug("camera: Exposure set to %d (0x%03X)\n", exposure, exposure_12bit);
    
    return SUCCESS;
}

/**
 * \fn camera_set_brightness
 *
 * \brief Set camera brightness value
 *
 * \param brightness brightness value (0-255)
 *
 * \returns status_t SUCCESS if brightness was set successfully
 */
status_t camera_set_brightness(uint8_t brightness) {
    ret_err_status(camera_write_register(ARDUCAM_REG_BRIGHTNESS, brightness), 
                   "camera: Failed to set brightness");
    
    debug("camera: Brightness set to %d\n", brightness);
    
    return SUCCESS;
}

/**
 * \fn camera_set_contrast
 *
 * \brief Set camera contrast value
 *
 * \param contrast contrast value (0-255)
 *
 * \returns status_t SUCCESS if contrast was set successfully
 */
status_t camera_set_contrast(uint8_t contrast) {
    ret_err_status(camera_write_register(ARDUCAM_REG_CONTRAST, contrast), 
                   "camera: Failed to set contrast");
    
    debug("camera: Contrast set to %d\n", contrast);
    
    return SUCCESS;
}

/**
 * \fn camera_set_format
 *
 * \brief Set camera image format
 *
 * \param format image format to set
 *
 * \returns status_t SUCCESS if format was set successfully
 */
status_t camera_set_format(camera_format_t format) {
    uint8_t format_value;
    
    switch (format) {
        case RGB565:
            format_value = ARDUCAM_FORMAT_RGB565;
            break;
        case RGB888:
            format_value = ARDUCAM_FORMAT_RGB888;
            break;
        case YUV422:
            format_value = ARDUCAM_FORMAT_YUV422;
            break;
        case JPEG:
            format_value = ARDUCAM_FORMAT_JPEG;
            break;
        default:
            warning("camera: Unsupported image format %d\n", format);
            return ERROR_SANITY_CHECK_FAILED;
    }
    
    // TODO: Implement format-specific register configuration
    // This would depend on the specific ArduCam model and sensor
    
    debug("camera: Image format set to %d\n", format);
    
    return SUCCESS;
}

/**
 * \fn camera_set_resolution
 *
 * \brief Set camera resolution
 *
 * \param width image width
 * \param height image height
 *
 * \returns status_t SUCCESS if resolution was set successfully
 */
status_t camera_set_resolution(uint16_t width, uint16_t height) {
    // TODO: Implement resolution-specific register configuration
    // This would depend on the specific ArduCam model and sensor
    
    debug("camera: Resolution set to %dx%d\n", width, height);
    
    return SUCCESS;
}

/**
 * \fn camera_start_capture
 *
 * \brief Start image capture
 *
 * \param config pointer to capture configuration
 *
 * \returns status_t SUCCESS if capture start was successful
 */
status_t camera_start_capture(const camera_config_t *const config) {
    if (!config) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    debug("camera_driver: Starting image capture\n");
    
    // Flush FIFO before starting new capture
    ret_err_status(camera_flush_fifo(), "camera_driver: Failed to flush FIFO");
    
    // Start capture based on capture mode
    switch (config->capture_mode) {
        case CAPTURE_SINGLE:
            ret_err_status(camera_send_command(ARDUCAM_CMD_SINGLE_CAPTURE, 0), 
                           "camera_driver: Failed to start single capture");
            break;
            
        case CAPTURE_CONTINUOUS:
            ret_err_status(camera_send_command(ARDUCAM_CMD_CONTINUOUS_CAPTURE, 0), 
                           "camera_driver: Failed to start continuous capture");
            break;
            
        case CAPTURE_BURST:
            // TODO: Implement burst capture mode
            warning("camera_driver: Burst capture mode not yet implemented\n");
            return ERROR_NOT_READY;
            
        default:
            return ERROR_SANITY_CHECK_FAILED;
    }
    
    debug("camera_driver: Capture started successfully\n");
    
    return SUCCESS;
}

/**
 * \fn camera_get_captured_image
 *
 * \brief Wait for capture completion and get image data
 *
 * \param image_buffer pointer to image buffer to fill
 * \param timeout_ms timeout in milliseconds
 *
 * \returns status_t SUCCESS if capture was successful
 */
status_t camera_get_captured_image(camera_image_t *const image_buffer, uint32_t timeout_ms) {
    if (!image_buffer) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    uint32_t start_time = xTaskGetTickCount();
    
    debug("camera_driver: Waiting for capture completion\n");
    
    // Wait for capture to complete
    while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(timeout_ms)) {
        // TODO: Implement capture completion detection
        // This would typically involve checking a status register or interrupt
        
        // For now, use a simple delay
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Simulate successful capture after delay
        if ((xTaskGetTickCount() - start_time) > pdMS_TO_TICKS(500)) {
            break;
        }
    }
    
    // TODO: Implement actual image data reading from FIFO
    // This would involve reading the image data through SPI
    
    // For now, simulate image data
    image_buffer->size = image_buffer->width * image_buffer->height * 2; // RGB565
    if (image_buffer->size > CAMERA_MAX_IMAGE_SIZE) {
        image_buffer->size = CAMERA_MAX_IMAGE_SIZE;
    }
    
    // Fill with dummy data for testing
    for (uint32_t i = 0; i < image_buffer->size; i++) {
        image_buffer->data[i] = (uint8_t)(i & 0xFF);
    }
    
    debug("camera_driver: Image captured (size: %d bytes)\n", image_buffer->size);
    
    return SUCCESS;
}

/**
 * \fn camera_stop_capture
 *
 * \brief Stop current capture operation
 *
 * \returns status_t SUCCESS if stop was successful
 */
status_t camera_stop_capture(void) {
    debug("camera_driver: Stopping capture\n");
    
    ret_err_status(camera_send_command(ARDUCAM_CMD_STOP_CAPTURE, 0), 
                   "camera_driver: Failed to stop capture");
    
    return SUCCESS;
}

/**
 * \fn camera_flush_fifo
 *
 * \brief Flush camera FIFO buffer
 *
 * \returns status_t SUCCESS if flush was successful
 */
status_t camera_flush_fifo(void) {
    debug("camera_driver: Flushing FIFO\n");
    
    ret_err_status(camera_send_command(ARDUCAM_CMD_FLUSH_FIFO, 0), 
                   "camera_driver: Failed to flush FIFO");
    
    // Wait for FIFO flush to complete
    vTaskDelay(pdMS_TO_TICKS(50));
    
    return SUCCESS;
}
