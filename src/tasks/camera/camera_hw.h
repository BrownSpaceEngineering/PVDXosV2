/**
 * camera_hw.h
 *
 * Camera hardware abstraction layer.
 * Provides low-level hardware interface for ArduCam communication.
 *
 * Created: January 24, 2025
 * Authors: PVDX Team
 */

#ifndef CAMERA_HW_H
#define CAMERA_HW_H

#include "globals.h"
#include "logging.h"
#include "atmel_start.h"

// ============================================================================
// HARDWARE CONSTANTS
// ============================================================================

// ArduCam SPI communication
#define CAMERA_HW_SPI_CS_PIN         Camera_CS
#define CAMERA_HW_SPI_INSTANCE       SPI_0

// ArduCam register addresses
#define CAMERA_HW_REG_SENSOR_ID      0x300A
#define CAMERA_HW_REG_CHIP_ID        0x300B
#define CAMERA_HW_REG_TIMING_CTRL    0x3820
#define CAMERA_HW_REG_EXPOSURE_H     0x3500
#define CAMERA_HW_REG_EXPOSURE_M     0x3501
#define CAMERA_HW_REG_EXPOSURE_L     0x3502
#define CAMERA_HW_REG_GAIN           0x350A
#define CAMERA_HW_REG_BRIGHTNESS     0x3507
#define CAMERA_HW_REG_CONTRAST       0x3508

// ArduCam SPI commands
#define CAMERA_HW_CMD_READ_REG       0x00
#define CAMERA_HW_CMD_WRITE_REG      0x01
#define CAMERA_HW_CMD_READ_FIFO      0x02
#define CAMERA_HW_CMD_CAPTURE        0x03
#define CAMERA_HW_CMD_FLUSH_FIFO     0x04
#define CAMERA_HW_CMD_SINGLE_CAPTURE 0x05
#define CAMERA_HW_CMD_CONTINUOUS_CAPTURE 0x06
#define CAMERA_HW_CMD_STOP_CAPTURE   0x07

// ArduCam status codes
#define CAMERA_HW_STATUS_OK          0x00
#define CAMERA_HW_STATUS_ERROR       0x01
#define CAMERA_HW_STATUS_BUSY        0x02
#define CAMERA_HW_STATUS_TIMEOUT     0x03

// Hardware timeouts
#define CAMERA_HW_INIT_TIMEOUT_MS     5000
#define CAMERA_HW_CAPTURE_TIMEOUT_MS  10000
#define CAMERA_HW_FIFO_FLUSH_TIMEOUT_MS 1000

// ============================================================================
// HARDWARE INITIALIZATION
// ============================================================================

/**
 * \brief Initialize camera hardware
 * \return status_t SUCCESS if hardware initialization was successful
 */
status_t camera_hw_init(void);

/**
 * \brief Deinitialize camera hardware
 * \return status_t SUCCESS if hardware deinitialization was successful
 */
status_t camera_hw_deinit(void);

// ============================================================================
// HARDWARE REGISTER OPERATIONS
// ============================================================================

/**
 * \brief Read a register from the camera sensor
 * \param reg_addr Register address to read
 * \param value Pointer to store the read value
 * \return status_t SUCCESS if read was successful
 */
status_t camera_hw_read_register(uint16_t reg_addr, uint8_t *value);

/**
 * \brief Write a value to a camera sensor register
 * \param reg_addr Register address to write
 * \param value Value to write
 * \return status_t SUCCESS if write was successful
 */
status_t camera_hw_write_register(uint16_t reg_addr, uint8_t value);

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
status_t camera_hw_spi_transaction(const uint8_t *tx_data, uint8_t *rx_data, uint16_t length);

/**
 * \brief Send command to camera
 * \param command Command to send
 * \param param Optional parameter for command
 * \return status_t SUCCESS if command was sent successfully
 */
status_t camera_hw_send_command(uint8_t command, uint8_t param);

/**
 * \brief Wait for camera to be ready
 * \param timeout_ms Timeout in milliseconds
 * \return status_t SUCCESS if camera is ready within timeout
 */
status_t camera_hw_wait_ready(uint32_t timeout_ms);

// ============================================================================
// HARDWARE CAPTURE OPERATIONS
// ============================================================================

/**
 * \brief Start image capture
 * \return status_t SUCCESS if capture start was successful
 */
status_t camera_hw_start_capture(void);

/**
 * \brief Stop current capture operation
 * \return status_t SUCCESS if stop was successful
 */
status_t camera_hw_stop_capture(void);

/**
 * \brief Flush camera FIFO buffer
 * \return status_t SUCCESS if flush was successful
 */
status_t camera_hw_flush_fifo(void);

/**
 * \brief Get captured image data
 * \param image_buffer Pointer to image buffer to fill
 * \param timeout_ms Timeout in milliseconds
 * \return status_t SUCCESS if image was captured successfully
 */
status_t camera_hw_get_captured_image(camera_image_t *image_buffer, uint32_t timeout_ms);

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================

/**
 * \brief Configure camera exposure
 * \param exposure Exposure value (0-255)
 * \return status_t SUCCESS if exposure was set successfully
 */
status_t camera_hw_set_exposure(uint8_t exposure);

/**
 * \brief Configure camera brightness
 * \param brightness Brightness value (0-255)
 * \return status_t SUCCESS if brightness was set successfully
 */
status_t camera_hw_set_brightness(uint8_t brightness);

/**
 * \brief Configure camera contrast
 * \param contrast Contrast value (0-255)
 * \return status_t SUCCESS if contrast was set successfully
 */
status_t camera_hw_set_contrast(uint8_t contrast);

/**
 * \brief Configure camera image format
 * \param format Image format to set
 * \return status_t SUCCESS if format was set successfully
 */
status_t camera_hw_set_format(camera_format_t format);

/**
 * \brief Configure camera resolution
 * \param width Image width
 * \param height Image height
 * \return status_t SUCCESS if resolution was set successfully
 */
status_t camera_hw_set_resolution(uint16_t width, uint16_t height);

// ============================================================================
// HARDWARE STATUS AND DIAGNOSTICS
// ============================================================================

/**
 * \brief Get camera sensor ID
 * \param sensor_id Pointer to store sensor ID
 * \return status_t SUCCESS if sensor ID was read successfully
 */
status_t camera_hw_get_sensor_id(uint16_t *sensor_id);

/**
 * \brief Get camera chip ID
 * \param chip_id Pointer to store chip ID
 * \return status_t SUCCESS if chip ID was read successfully
 */
status_t camera_hw_get_chip_id(uint16_t *chip_id);

/**
 * \brief Check if camera hardware is responding
 * \return status_t SUCCESS if camera is responding
 */
status_t camera_hw_check_communication(void);

#endif // CAMERA_HW_H
