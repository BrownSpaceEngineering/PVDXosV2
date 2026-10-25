/**
 * arducam_driver.c
 *
 * Driver for the Arducam OV2640 camera.
 *
 * Created: November 17, 2024
 * Authors: Alexander Thaep, Tanish Makadia, Zach Mahan
 */

#include "arducam_driver.h"

#include <string.h> // For memcpy

#include "arducam_registers.h"
#include "hpl_sercom_config.h"
#include "mutexes.h"
#include "task_list.h"
#include "watchdog_task.h"

// Buffer for SPI transactions
uint8_t ardu_spi_rx_buffer[ARDUCAM_SPI_RX_BUF_SIZE] = {0x00};
uint8_t ardu_spi_tx_buffer[ARDUCAM_SPI_TX_BUF_SIZE] = {0x00};
struct spi_xfer ardu_xfer = {.rxbuf = ardu_spi_rx_buffer, .txbuf = ardu_spi_tx_buffer, .size = 0};

struct io_descriptor *arducam_i2c_io;
struct io_descriptor *arducam_spi_io;

/**
 * \fn init_arducam_hardware
 *
 * \brief Initializes the arducam hardware
 *
 * \returns `status_t`, whether the operation was successful
 */
status_t init_arducam_hardware(void) {
    // Preserve the generated 100 kHz timing on the shared I2C bus. The slave
    // address is selected under the bus mutex for every transaction.
    lock_mutex(i2c_camera_bus_mutex);
    int32_t result = i2c_m_sync_get_io_descriptor(&I2C_CAMERA, &arducam_i2c_io);
    if (result == ERR_NONE) {
        result = i2c_m_sync_enable(&I2C_CAMERA);
    }
    unlock_mutex(i2c_camera_bus_mutex);
    if (result != ERR_NONE) {
        return ERROR_I2C_FAILED;
    }

    gpio_set_pin_direction(CAMERA_CS, GPIO_DIRECTION_OUT);
    gpio_set_pin_level(CAMERA_CS, 1);

    // ASF expects a raw BAUD divider, not Hz. This task exclusively owns SPI_CAMERA.
    spi_m_sync_disable(&SPI_CAMERA);
    if (spi_m_sync_set_baudrate(&SPI_CAMERA, CONF_GCLK_SERCOM2_CORE_FREQUENCY / (2U * 1000000U) - 1U) != ERR_NONE ||
        spi_m_sync_get_io_descriptor(&SPI_CAMERA, &arducam_spi_io) != ERR_NONE) {
        return ERROR_SPI_TRANSFER_FAILED;
    }
    spi_m_sync_enable(&SPI_CAMERA);

    // Write and read a known test pattern (0x55) into ArduCHIP internal test register
    if (arducam_spi_write(ARDUCHIP_TEST1, 0x55) != 2) {
        return ERROR_SPI_TRANSFER_FAILED;
    }
    uint8_t temp = 0;
    if (arducam_spi_read(ARDUCHIP_TEST1, &temp) != SUCCESS || temp != 0x55) {
        warning("Camera SPI interface error!");
        return ERROR_SPI_TRANSFER_FAILED;
    }

    // Save payload bytes as variables so we can pass by reference
    uint8_t data[2] = {0x00, 0x01};
    uint8_t config[2] = {0x01, 0x80};

    // Verify that the camera sensor is an OV2640 based on VID and PID
    uint8_t vid = 0, pid = 0;
    if (arducam_i2c_write(0xFF, &data[1], 1) != 2 || arducam_i2c_read(OV2640_CHIPID_HIGH, &vid, 1) != 1 ||
        arducam_i2c_read(OV2640_CHIPID_LOW, &pid, 1) != 1) {
        return ERROR_I2C_FAILED;
    }

    if (vid != 0x26 || (pid != 0x41 && pid != 0x42)) {
        warning("Unexpected OV2640 ID: vid=0x%02X pid=0x%02X", vid, pid);
        return ERROR_SANITY_CHECK_FAILED;
    }

    if (arducam_i2c_write(0xFF, &config[0], 1) != 2 || arducam_i2c_write(0x12, &config[1], 1) != 2) { // COM7 = 0x80: sensor soft reset
        return ERROR_I2C_FAILED;
    }

    // Wait for the sensor to finish resetting before loading the register tables.
    // Without this, the JPEG/size config is written while the sensor is still
    // resetting, so the resolution never takes and the sensor stays near its
    // power-up default (~176x144) regardless of the size table below.
    //
    // NOTE: Use vTaskDelay, NOT the ASF delay_ms(). delay_ms() busy-waits on
    // SysTick and overwrites SysTick->LOAD/VAL, which is the same timer FreeRTOS
    // uses for its scheduler tick -- calling it from a task corrupts the tick
    // rate and breaks every RTOS timeout. vTaskDelay yields without touching it.
    vTaskDelay(pdMS_TO_TICKS(100));

    // fmt to jpeg config
    if (arducam_i2c_multi_write(OV2640_JPEG_INIT) != ERR_NONE || arducam_i2c_multi_write(OV2640_YUV422) != ERR_NONE ||
        arducam_i2c_multi_write(OV2640_JPEG) != ERR_NONE || arducam_i2c_write(0xFF, &data[1], 1) != 2 ||
        arducam_i2c_write(0x15, &data[0], 1) != 2 || arducam_i2c_multi_write(OV2640_1600x1200_JPEG) != ERR_NONE) {
        return ERROR_I2C_FAILED;
    }

    return SUCCESS;
}

int32_t arducam_i2c_write(uint8_t addr, uint8_t *data, uint16_t size) {
    uint8_t writeBuf[32 + 1];
    if (size > sizeof(writeBuf) - 1U) {
        return ERR_INVALID_ARG;
    }
    writeBuf[0] = addr;
    memcpy(writeBuf + 1, data, size);
    int32_t rv;
    lock_mutex(i2c_camera_bus_mutex);
    i2c_m_sync_set_slaveaddr(&I2C_CAMERA, ARDUCAM_ADDR >> 1, I2C_M_SEVEN);
    rv = io_write(arducam_i2c_io, writeBuf, size + 1);
    unlock_mutex(i2c_camera_bus_mutex);
    if (rv < 0) {
        warning("Error in Arducam Write");
    }
    return rv;
}

int32_t arducam_i2c_multi_write(const struct sensor_reg reglist[]) {
    for (const struct sensor_reg *next = reglist; next->reg != 0xff || next->val != 0xff; next++) {
        uint8_t value = next->val;
        if (arducam_i2c_write(next->reg, &value, 1) != 2) {
            return ERR_IO;
        }
    }
    return ERR_NONE;
}

int32_t arducam_i2c_read(uint8_t addr, uint8_t *readBuf, uint16_t size) {
    uint8_t writeBuf[1] = {addr};
    int32_t rv;
    lock_mutex(i2c_camera_bus_mutex);
    i2c_m_sync_set_slaveaddr(&I2C_CAMERA, ARDUCAM_ADDR >> 1, I2C_M_SEVEN);
    rv = io_write(arducam_i2c_io, writeBuf, 1);
    if (rv != 1) {
        unlock_mutex(i2c_camera_bus_mutex);
        warning("Error in Arducam Write");
        return rv < 0 ? rv : ERR_IO;
    }
    if ((rv = io_read(arducam_i2c_io, readBuf, size)) < 0) {
        warning("Error in Arducam Read");
    }
    unlock_mutex(i2c_camera_bus_mutex);
    return rv;
}

int32_t arducam_spi_write(uint8_t addr, uint8_t data) {
    gpio_set_pin_level(CAMERA_CS, 0);

    uint8_t value[2] = {addr | 0x80, data}; // Set MSB high for write operation
    int32_t response = io_write(arducam_spi_io, value, 2);

    gpio_set_pin_level(CAMERA_CS, 1);
    return response;
}

status_t arducam_spi_read(uint8_t addr, uint8_t *data) {
    if (data == NULL) {
        return ERROR_BAD_ARGS;
    }
    gpio_set_pin_level(CAMERA_CS, 0);
    uint8_t tx_data = addr & 0x7F; // Set MSB low for read operation
    status_t status = SUCCESS;
    if (io_write(arducam_spi_io, &tx_data, 1) != 1 || io_read(arducam_spi_io, data, 1) != 1) {
        status = ERROR_SPI_TRANSFER_FAILED;
    }
    gpio_set_pin_level(CAMERA_CS, 1);
    return status;
}

static status_t read_fifo_length(size_t *length) {
    uint8_t len1, len2, len3;
    if (arducam_spi_read(FIFO_SIZE1, &len1) != SUCCESS || arducam_spi_read(FIFO_SIZE2, &len2) != SUCCESS ||
        arducam_spi_read(FIFO_SIZE3, &len3) != SUCCESS) {
        return ERROR_SPI_TRANSFER_FAILED;
    }
    *length = ((uint32_t)(len3 & 0x7f) << 16) | ((uint32_t)len2 << 8) | len1;
    return SUCCESS;
}

/**
 * \fn wait_capture_done
 *
 * \brief Polls ARDUCHIP_TRIG until the capture-done flag is set, yielding between polls
 *
 * \returns `status_t`, ERROR_NOT_READY if the capture did not finish within ARDUCAM_CAPTURE_TIMEOUT_MS
 */
static status_t wait_capture_done(arducam_cancel_fn cancelled) {
    const TickType_t start = xTaskGetTickCount();
    while (true) {
        if (cancelled != NULL && cancelled()) {
            return ERROR_TASK_DISABLED;
        }
        uint8_t trigger;
        ret_err_status(arducam_spi_read(ARDUCHIP_TRIG, &trigger), "Arducam trigger read failed");
        if (trigger & CAP_DONE_MASK) {
            return SUCCESS;
        }
        if (xTaskGetTickCount() - start >= pdMS_TO_TICKS(ARDUCAM_CAPTURE_TIMEOUT_MS)) {
            warning("Arducam capture timed out!");
            return ERROR_NOT_READY;
        }
        vTaskDelay(1);
    }
    return SUCCESS;
}

status_t capture(void) {
    // NOTE: In some versions of the official C++ ArduCam OV2640 driver, the FIFO_CLEAR_MASK is written twice,
    // while in others, it is written once. This implementation emprically works, so currently it is written twice.
    arducam_spi_write(ARDUCHIP_FIFO, FIFO_CLEAR_MASK); // Flush the FIFO
    arducam_spi_write(ARDUCHIP_FIFO, FIFO_CLEAR_MASK); // Clear the capture done flag (uses same clear mask)
    arducam_spi_write(ARDUCHIP_FIFO, FIFO_START_MASK); // Start capture
    ret_err_status(wait_capture_done(NULL), "Arducam capture failed");

    // Read how many bytes were captured
    size_t len;
    ret_err_status(read_fifo_length(&len), "Arducam FIFO length read failed");
    if (len >= OV2640_MAX_FIFO_SIZE) {
        info("Oversized!");
        return ERROR_SANITY_CHECK_FAILED;
    } else if (len == 0) {
        info("Size is 0!");
        return ERROR_SANITY_CHECK_FAILED;
    }

    // Select SPI peripheral device
    gpio_set_pin_level(CAMERA_CS, 0);

    // Start burst read
    uint8_t cmd = BURST_FIFO_READ;
    io_write(arducam_spi_io, &cmd, 1);

    // Send dummy byte before looping (official C++ driver does this for some reason)
    uint8_t dummy = 0x00;
    io_write(arducam_spi_io, &dummy, 1);

    // Iteratively transfer image bytes into our rx buffer
    size_t bufferSize = ARDUCAM_SPI_RX_BUF_SIZE;
    ardu_xfer.txbuf = NULL;
    ardu_xfer.rxbuf = ardu_spi_rx_buffer;

    while (len) {
        size_t will_copy = (len < bufferSize) ? len : bufferSize;
        ardu_xfer.size = will_copy;
        spi_m_sync_transfer(&SPI_CAMERA, &ardu_xfer);
        len -= will_copy;
    }

    // Deselect SPI peripheral device
    gpio_set_pin_level(CAMERA_CS, 1);
    return SUCCESS;
}

// CRC state is kept uncomplemented internally so DATA chunks can extend the frame CRC.
static uint32_t image_crc32(uint32_t crc, const uint8_t *bytes, size_t size) {
    while (size--) {
        crc ^= *bytes++;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return crc;
}

static void put_le32(uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes[i] = (uint8_t)(value >> (8U * i));
    }
}

enum {
    IMAGE_BEGIN = 1,
    IMAGE_DATA = 2,
    IMAGE_END = 3,
    IMAGE_ABORT = 4
};

static void image_header(uint8_t type, uint16_t size, uint32_t frame_id, uint32_t offset, uint32_t total) {
    uint8_t *header = ardu_spi_rx_buffer;
    memcpy(header, "PVIM", 4);
    header[4] = 1;
    header[5] = type;
    header[6] = (uint8_t)size;
    header[7] = (uint8_t)(size >> 8);
    put_le32(header + 8, frame_id);
    put_le32(header + 12, offset);
    put_le32(header + 16, total);
    put_le32(header + 20, ~image_crc32(UINT32_MAX, header + ARDUCAM_RTT_HEADER_SIZE, size));
    put_le32(header + 24, ~image_crc32(UINT32_MAX, header, 24));
}

static status_t image_write_packet(size_t payload_size, arducam_cancel_fn cancelled) {
    const unsigned size = ARDUCAM_RTT_HEADER_SIZE + payload_size;
    const TickType_t start = xTaskGetTickCount();
    while (true) {
        if (cancelled != NULL && cancelled()) {
            return ERROR_TASK_DISABLED;
        }
        // NO_BLOCK_SKIP accepts the entire packet or nothing. Keep the buffer intact on retries.
        if (SEGGER_RTT_Write(CAMERA_RTT_OUTPUT_CHANNEL, ardu_spi_rx_buffer, size) == size) {
            return SUCCESS;
        }
        if ((TickType_t)(xTaskGetTickCount() - start) >= pdMS_TO_TICKS(ARDUCAM_RTT_TIMEOUT_MS)) {
            warning("camera: RTT image stream timed out");
            return ERROR_WRITE_FAILED;
        }
        vTaskDelay(1);
    }
}

status_t capture_rtt_cancellable(arducam_cancel_fn cancelled) {
    static uint32_t next_frame_id;
    const uint32_t frame_id = ++next_frame_id;
    uint32_t offset = 0, frame_crc = UINT32_MAX;
    size_t total = 0;
    bool begun = false, selected = false;
    status_t status = SUCCESS;
    uint8_t *const payload = ardu_spi_rx_buffer + ARDUCAM_RTT_HEADER_SIZE;

    if (cancelled != NULL && cancelled()) {
        return ERROR_TASK_DISABLED;
    }
    // Preserve the driver's double-clear sequence before starting capture.
    if (arducam_spi_write(ARDUCHIP_FIFO, FIFO_CLEAR_MASK) != 2 || arducam_spi_write(ARDUCHIP_FIFO, FIFO_CLEAR_MASK) != 2 ||
        arducam_spi_write(ARDUCHIP_FIFO, FIFO_START_MASK) != 2) {
        return ERROR_SPI_TRANSFER_FAILED;
    }
    ret_err_status(wait_capture_done(cancelled), "Arducam capture failed");
    ret_err_status(read_fifo_length(&total), "Arducam FIFO length read failed");
    if (total == 0 || total >= OV2640_MAX_FIFO_SIZE) {
        return ERROR_SANITY_CHECK_FAILED;
    }

    // BEGIN payload: little-endian UXGA width and height.
    payload[0] = 0x40;
    payload[1] = 0x06;
    payload[2] = 0xb0;
    payload[3] = 0x04;
    image_header(IMAGE_BEGIN, 4, frame_id, 0, total);
    status = image_write_packet(4, cancelled);
    if (status != SUCCESS) {
        goto cleanup;
    }
    begun = true;
    if (cancelled != NULL && cancelled()) {
        status = ERROR_TASK_DISABLED;
        goto cleanup;
    }
    gpio_set_pin_level(CAMERA_CS, 0);
    selected = true;
    uint8_t cmd = BURST_FIFO_READ;
    if (io_write(arducam_spi_io, &cmd, 1) != 1) {
        status = ERROR_SPI_TRANSFER_FAILED;
        goto cleanup;
    }

    // The first byte after BURST_FIFO_READ is JPEG data; do not clock a dummy byte.
    ardu_xfer.txbuf = NULL;
    ardu_xfer.rxbuf = payload;
    while (offset < total) {
        if (cancelled != NULL && cancelled()) {
            status = ERROR_TASK_DISABLED;
            goto cleanup;
        }
        const size_t remaining = total - offset;
        const size_t size = remaining < ARDUCAM_RTT_MAX_PAYLOAD ? remaining : ARDUCAM_RTT_MAX_PAYLOAD;
        ardu_xfer.size = size;
        if (spi_m_sync_transfer(&SPI_CAMERA, &ardu_xfer) != (int32_t)size) {
            status = ERROR_SPI_TRANSFER_FAILED;
            goto cleanup;
        }
        frame_crc = image_crc32(frame_crc, payload, size);
        image_header(IMAGE_DATA, size, frame_id, offset, total);
        status = image_write_packet(size, cancelled);
        if (status != SUCCESS) {
            goto cleanup;
        }
        offset += size;
        pvdx_task_t *const current_task = get_current_task();
        if (current_task != NULL && should_checkin(current_task)) {
            watchdog_checkin(current_task);
        }
    }
    gpio_set_pin_level(CAMERA_CS, 1);
    selected = false;
    put_le32(payload, ~frame_crc);
    image_header(IMAGE_END, 4, frame_id, offset, total);
    status = image_write_packet(4, cancelled);

cleanup:
    if (selected) {
        gpio_set_pin_level(CAMERA_CS, 1);
    }
    if (status != SUCCESS && begun) {
        put_le32(payload, (uint32_t)status);
        image_header(IMAGE_ABORT, 4, frame_id, offset, total);
        // Best effort only: a stopped host must not delay cancellation or the next capture.
        (void)SEGGER_RTT_Write(CAMERA_RTT_OUTPUT_CHANNEL, ardu_spi_rx_buffer, ARDUCAM_RTT_HEADER_SIZE + 4U);
    }
    return status;
}

status_t capture_rtt(void) {
    return capture_rtt_cancellable(NULL);
}
