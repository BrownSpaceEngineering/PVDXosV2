/**
 * arducam_driver.c
 *
 * Driver for the Arducam OV2640 camera.
 *
 * Created: November 17, 2024
 * Authors: Alexander Thaep, Tanish Makadia, Zach Mahan
 */

#include "arducam_driver.h"

#include <string.h>

#include "arducam_registers.h"
#include "arducam_task.h"
#include "peripheral_clk_config.h"
#include "drivers/pmb/pmb_i2c.h"
#include "mutexes.h"

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
static status_t init_arducam_hardware_locked(void) {
    // Keep the generated I2C timing: SERCOM6 is also used by the PMB on main.
    if (pmb_i2c_init() != SUCCESS || i2c_m_sync_get_io_descriptor(&I2C_CAMERA, &arducam_i2c_io) < 0) {
        return ERROR_I2C_FAILED;
    }

    gpio_set_pin_direction(CAMERA_CS, GPIO_DIRECTION_OUT);
    gpio_set_pin_level(CAMERA_CS, 1);

    // ASF takes the hardware BAUD divider, not a frequency in Hz. SPI_CAMERA is SERCOM2 on main.
    const uint32_t spi_baud = CONF_GCLK_SERCOM2_CORE_FREQUENCY / (2U * 1000000U) - 1U;
    if (spi_m_sync_set_baudrate(&SPI_CAMERA, spi_baud) < 0 || spi_m_sync_get_io_descriptor(&SPI_CAMERA, &arducam_spi_io) < 0) {
        return ERROR_SPI_TRANSFER_FAILED;
    }
    spi_m_sync_enable(&SPI_CAMERA);

    // Write and read a known test pattern (0x55) into ArduCHIP internal test register
    if (arducam_spi_write(ARDUCHIP_TEST1, 0x55) != 2 || arducam_spi_read(ARDUCHIP_TEST1) != 0x55) {
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

    if (arducam_i2c_write(0xFF, &config[0], 1) != 2 || arducam_i2c_write(0x12, &config[1], 1) != 2) {
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
    if (arducam_i2c_multi_write(OV2640_JPEG_INIT) < 0 || arducam_i2c_multi_write(OV2640_YUV422) < 0 ||
        arducam_i2c_multi_write(OV2640_JPEG) < 0 || arducam_i2c_write(0xFF, &data[1], 1) != 2 ||
        arducam_i2c_write(0x15, &data[0], 1) != 2 || arducam_i2c_multi_write(OV2640_320x240_JPEG) < 0) {
        return ERROR_I2C_FAILED;
    }

    return SUCCESS;
}

status_t init_arducam_hardware(void) {
    lock_camera_operation();
    status_t status = init_arducam_hardware_locked();
    gpio_set_pin_level(CAMERA_CS, 1);
    unlock_camera_operation();
    return status;
}

int32_t arducam_i2c_write(uint8_t addr, const uint8_t *data, uint16_t size) {
    uint8_t writeBuf[32 + 1];
    if (data == NULL || size == 0 || size > sizeof(writeBuf) - 1) {
        return ERR_INVALID_ARG;
    }
    writeBuf[0] = addr;
    memcpy(writeBuf + 1, data, size);
    // Per-message addressing avoids changing the default slave address of the shared PMB bus.
    struct _i2c_m_msg msg = {
        .addr = ARDUCAM_ADDR >> 1,
        .len = size + 1,
        .buffer = writeBuf,
        .flags = I2C_M_SEVEN | I2C_M_STOP,
    };
    lock_camera_pmb_i2c();
    int32_t rv = _i2c_m_sync_transfer(&I2C_CAMERA.device, &msg);
    unlock_camera_pmb_i2c();
    return rv < 0 ? rv : size + 1;
}

int32_t arducam_i2c_multi_write(const struct sensor_reg reglist[]) {
    for (const struct sensor_reg *next = reglist; next->reg != 0xff || next->val != 0xff; next++) {
        int32_t rv = arducam_i2c_write(next->reg, &next->val, 1);
        if (rv != 2) {
            return rv < 0 ? rv : ERR_IO;
        }
    }
    return 0;
}

int32_t arducam_i2c_read(uint8_t addr, uint8_t *readBuf, uint16_t size) {
    if (readBuf == NULL || size == 0) {
        return ERR_INVALID_ARG;
    }
    struct _i2c_m_msg wr = {
        .addr = ARDUCAM_ADDR >> 1,
        .len = 1,
        .buffer = &addr,
        .flags = I2C_M_SEVEN,
    };
    struct _i2c_m_msg rd = {
        .addr = ARDUCAM_ADDR >> 1,
        .len = size,
        .buffer = readBuf,
        .flags = I2C_M_SEVEN | I2C_M_RD | I2C_M_STOP,
    };
    lock_camera_pmb_i2c();
    int32_t rv = _i2c_m_sync_transfer(&I2C_CAMERA.device, &wr);
    if (rv == 0) {
        rv = _i2c_m_sync_transfer(&I2C_CAMERA.device, &rd);
    }
    unlock_camera_pmb_i2c();
    return rv < 0 ? rv : size;
}

int32_t arducam_spi_write(uint8_t addr, uint8_t data) {
    gpio_set_pin_level(CAMERA_CS, 0);

    uint8_t value[2] = {addr | 0x80, data}; // Set MSB high for write operation
    int32_t response = io_write(arducam_spi_io, value, 2);

    gpio_set_pin_level(CAMERA_CS, 1);
    return response;
}

int32_t arducam_spi_read(uint8_t addr) {
    gpio_set_pin_level(CAMERA_CS, 0);

    uint8_t tx_data = addr & 0x7F; // Set MSB low for read operation
    uint8_t rx_data = 0;

    int32_t rv = io_write(arducam_spi_io, &tx_data, 1);
    if (rv == 1) {
        rv = io_read(arducam_spi_io, &rx_data, 1);
    }
    gpio_set_pin_level(CAMERA_CS, 1);
    return rv == 1 ? rx_data : (rv < 0 ? rv : ERR_IO);
}

static uint32_t read_fifo_length(void) {
    int32_t len1 = arducam_spi_read(FIFO_SIZE1);
    int32_t len2 = arducam_spi_read(FIFO_SIZE2);
    int32_t len3 = arducam_spi_read(FIFO_SIZE3);
    if (len1 < 0 || len2 < 0 || len3 < 0) {
        return 0;
    }
    return (((uint32_t)len3 << 16) | ((uint32_t)len2 << 8) | (uint32_t)len1) & 0x07fffff;
}

static bool start_capture(void) {
    // Retain the branch's tested double-clear sequence.
    if (arducam_spi_write(ARDUCHIP_FIFO, FIFO_CLEAR_MASK) != 2 || arducam_spi_write(ARDUCHIP_FIFO, FIFO_CLEAR_MASK) != 2 ||
        arducam_spi_write(ARDUCHIP_FIFO, FIFO_START_MASK) != 2) {
        warning("arducam: Failed to start capture\n");
        return false;
    }
    const TickType_t start = xTaskGetTickCount();
    while (true) {
        int32_t trigger = arducam_spi_read(ARDUCHIP_TRIG);
        if (trigger < 0) {
            warning("arducam: Failed to read capture status\n");
            return false;
        }
        if (trigger & CAP_DONE_MASK) {
            return true;
        }
        if ((TickType_t)(xTaskGetTickCount() - start) >= pdMS_TO_TICKS(2000)) {
            warning("arducam: Capture timed out\n");
            return false;
        }
        vTaskDelay(1);
    }
}

static void capture_image_locked(bool stream_rtt) {
    if (!start_capture()) {
        return;
    }
    size_t len = read_fifo_length();
    if (len == 0 || len >= OV2640_MAX_FIFO_SIZE) {
        warning("arducam: Invalid FIFO length %u\n", (unsigned)len);
        return;
    }

    // Keep these markers compatible with scripts/rtt_logs.py.
    if (stream_rtt) {
        info("IMG_BEGIN len=%u", (unsigned)len);
    }
    gpio_set_pin_level(CAMERA_CS, 0);
    uint8_t cmd = BURST_FIFO_READ;
    bool complete = io_write(arducam_spi_io, &cmd, 1) == 1;
    const TickType_t start = xTaskGetTickCount();
    ardu_xfer.txbuf = NULL;
    ardu_xfer.rxbuf = ardu_spi_rx_buffer;

    // No extra dummy byte: the first clock after BURST_FIFO_READ returns the JPEG's leading 0xFF.
    while (complete && len != 0) {
        size_t will_copy = len < ARDUCAM_SPI_RX_BUF_SIZE ? len : ARDUCAM_SPI_RX_BUF_SIZE;
        ardu_xfer.size = will_copy;
        if (spi_m_sync_transfer(&SPI_CAMERA, &ardu_xfer) != (int32_t)will_copy) {
            complete = false;
            break;
        }
        size_t sent = 0;
        while (stream_rtt && sent < will_copy) {
            // init_arducam configures NO_BLOCK_TRIM: a disconnected host must not stall the scheduler.
            sent += SEGGER_RTT_Write(CAMERA_RTT_OUTPUT_CHANNEL, ardu_spi_rx_buffer + sent, (unsigned)(will_copy - sent));
            if ((TickType_t)(xTaskGetTickCount() - start) >= pdMS_TO_TICKS(2000)) {
                complete = false;
                break;
            }
            if (sent < will_copy) {
                vTaskDelay(1);
            }
        }
        len -= will_copy;
        // Yield even when RTT drains immediately, so peer tasks get CPU time.
        vTaskDelay(1);
    }
    gpio_set_pin_level(CAMERA_CS, 1);
    if (!complete) {
        warning("arducam: Image transfer aborted (SPI error or RTT timeout)\n");
    }
    if (stream_rtt) {
        info("IMG_END");
    }
}

static void capture_image(bool stream_rtt) {
    lock_camera_operation();
    capture_image_locked(stream_rtt);
    gpio_set_pin_level(CAMERA_CS, 1);
    unlock_camera_operation();
}

void capture(void) {
    capture_image(false);
}

void capture_rtt(void) {
    capture_image(true);
}
