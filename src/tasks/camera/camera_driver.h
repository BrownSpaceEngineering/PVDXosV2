#ifndef CAMERA_DRIVER_H
#define CAMERA_DRIVER_H

#include "../../ASF/atmel_start.h"
#include "../../globals.h"
#include "../../misc/logging/logging.h"
#include "camera_types.h"

// ArduCam SPI communication constants
#define ARDUCAM_SPI_CS_PIN Camera_CS
#define ARDUCAM_SPI_INSTANCE SPI_0

// ArduCam register addresses and commands
#define ARDUCAM_REG_SENSOR_ID 0x300A
#define ARDUCAM_REG_CHIP_ID 0x300B
#define ARDUCAM_REG_TIMING_CTRL 0x3820
#define ARDUCAM_REG_EXPOSURE_H 0x3500
#define ARDUCAM_REG_EXPOSURE_M 0x3501
#define ARDUCAM_REG_EXPOSURE_L 0x3502
#define ARDUCAM_REG_GAIN 0x350A
#define ARDUCAM_REG_BRIGHTNESS 0x3507
#define ARDUCAM_REG_CONTRAST 0x3508

// ArduCam SPI commands
#define ARDUCAM_CMD_READ_REG 0x00
#define ARDUCAM_CMD_WRITE_REG 0x01
#define ARDUCAM_CMD_READ_FIFO 0x02
#define ARDUCAM_CMD_CAPTURE 0x03
#define ARDUCAM_CMD_FLUSH_FIFO 0x04
#define ARDUCAM_CMD_SINGLE_CAPTURE 0x05
#define ARDUCAM_CMD_CONTINUOUS_CAPTURE 0x06
#define ARDUCAM_CMD_STOP_CAPTURE 0x07

// ArduCam status codes
#define ARDUCAM_STATUS_OK 0x00
#define ARDUCAM_STATUS_ERROR 0x01
#define ARDUCAM_STATUS_BUSY 0x02
#define ARDUCAM_STATUS_TIMEOUT 0x03

// Camera initialization and configuration
#define ARDUCAM_INIT_TIMEOUT_MS 5000
#define ARDUCAM_CAPTURE_TIMEOUT_MS 10000
#define ARDUCAM_FIFO_FLUSH_TIMEOUT_MS 1000

// Image format configurations
#define ARDUCAM_FORMAT_RGB565 0x00
#define ARDUCAM_FORMAT_RGB888 0x01
#define ARDUCAM_FORMAT_YUV422 0x02
#define ARDUCAM_FORMAT_JPEG 0x03

// Function declarations

/**
 * \brief Initialize ArduCam hardware and SPI communication
 * \return status_t SUCCESS if initialization was successful
 */
status_t init_camera_hardware(void);

/**
 * \brief Read a register from the ArduCam sensor
 * \param reg_addr Register address to read
 * \param value Pointer to store the read value
 * \return status_t SUCCESS if read was successful
 */
status_t camera_read_register(uint16_t reg_addr, uint8_t *value);

/**
 * \brief Write a value to an ArduCam sensor register
 * \param reg_addr Register address to write
 * \param value Value to write
 * \return status_t SUCCESS if write was successful
 */
status_t camera_write_register(uint16_t reg_addr, uint8_t value);

/**
 * \brief Configure camera settings (exposure, brightness, contrast, format)
 * \param config Pointer to camera configuration structure
 * \return status_t SUCCESS if configuration was successful
 */
status_t camera_configure_settings(const camera_config_t *const config);

/**
 * \brief Start image capture
 * \param config Pointer to capture configuration
 * \return status_t SUCCESS if capture start was successful
 */
status_t camera_start_capture(const camera_config_t *const config);

/**
 * \brief Wait for capture completion and get image data
 * \param image_buffer Pointer to image buffer to fill
 * \param timeout_ms Timeout in milliseconds
 * \return status_t SUCCESS if capture was successful
 */
status_t camera_get_captured_image(camera_image_t *const image_buffer, uint32_t timeout_ms);

/**
 * \brief Stop current capture operation
 * \return status_t SUCCESS if stop was successful
 */
status_t camera_stop_capture(void);

/**
 * \brief Flush camera FIFO buffer
 * \return status_t SUCCESS if flush was successful
 */
status_t camera_flush_fifo(void);

/**
 * \brief Get camera sensor ID and verify communication
 * \param sensor_id Pointer to store sensor ID
 * \return status_t SUCCESS if sensor ID was read successfully
 */
status_t camera_get_sensor_id(uint16_t *sensor_id);

/**
 * \brief Set camera exposure value
 * \param exposure Exposure value (0-255)
 * \return status_t SUCCESS if exposure was set successfully
 */
status_t camera_set_exposure(uint8_t exposure);

/**
 * \brief Set camera brightness value
 * \param brightness Brightness value (0-255)
 * \return status_t SUCCESS if brightness was set successfully
 */
status_t camera_set_brightness(uint8_t brightness);

/**
 * \brief Set camera contrast value
 * \param contrast Contrast value (0-255)
 * \return status_t SUCCESS if contrast was set successfully
 */
status_t camera_set_contrast(uint8_t contrast);

/**
 * \brief Set camera image format
 * \param format Image format to set
 * \return status_t SUCCESS if format was set successfully
 */
status_t camera_set_format(camera_format_t format);

/**
 * \brief Set camera resolution
 * \param width Image width
 * \param height Image height
 * \return status_t SUCCESS if resolution was set successfully
 */
status_t camera_set_resolution(uint16_t width, uint16_t height);

/**
 * \brief Perform SPI transaction with ArduCam
 * \param tx_data Pointer to data to transmit
 * \param rx_data Pointer to buffer for received data
 * \param length Number of bytes to transfer
 * \return status_t SUCCESS if SPI transaction was successful
 */
status_t camera_spi_transaction(const uint8_t *tx_data, uint8_t *rx_data, uint16_t length);

/**
 * \brief Send command to ArduCam
 * \param command Command to send
 * \param param Optional parameter for command
 * \return status_t SUCCESS if command was sent successfully
 */
status_t camera_send_command(uint8_t command, uint8_t param);

/**
 * \brief Wait for ArduCam to be ready
 * \param timeout_ms Timeout in milliseconds
 * \return status_t SUCCESS if camera is ready within timeout
 */
status_t camera_wait_ready(uint32_t timeout_ms);

#endif // CAMERA_DRIVER_H
