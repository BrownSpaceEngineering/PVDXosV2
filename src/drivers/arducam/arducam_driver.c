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
#include "task_list.h"
#include "watchdog_task.h"

// Buffer for SPI transactions
uint8_t ardu_spi_rx_buffer[ARDUCAM_SPI_RX_BUF_SIZE] = {0x00};
uint8_t camera_rtt_image_buffer[SEGGER_RTT_IMAGE_BUFFER_SIZE];
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
    // Configure ASF I2C and SPI drivers for this device
    // Baud rates MUST match Atmel-Start config baudrates
    i2c_m_sync_set_baudrate(&I2C_CAMERA, 0, 100000);
    i2c_m_sync_get_io_descriptor(&I2C_CAMERA, &arducam_i2c_io);
    i2c_m_sync_enable(&I2C_CAMERA);
    i2c_m_sync_set_slaveaddr(&I2C_CAMERA, ARDUCAM_ADDR >> 1, I2C_M_SEVEN);

    gpio_set_pin_direction(CAMERA_CS, GPIO_DIRECTION_OUT);
    gpio_set_pin_level(CAMERA_CS, 1);

    spi_m_sync_set_baudrate(&SPI_CAMERA, 1000000);
    spi_m_sync_get_io_descriptor(&SPI_CAMERA, &arducam_spi_io);
    spi_m_sync_enable(&SPI_CAMERA);
    
    // Write and read a known test pattern (0x55) into ArduCHIP internal test register
    arducam_spi_write(ARDUCHIP_TEST1, 0x55);
    int temp = arducam_spi_read(ARDUCHIP_TEST1);
    if (temp != 0x55){
        warning("Camera SPI interface error!");
        return ERROR_SPI_TRANSFER_FAILED;
    }

    // Save payload bytes as variables so we can pass by reference
    uint8_t data[2] = { 0x00, 0x01 };
    uint8_t config[2] = { 0x01, 0x80 };
    
    // Verify that the camera sensor is an OV2640 based on VID and PID
    uint8_t vid = 0, pid = 0;
    arducam_i2c_write(0xFF, &data[1], 1);
    uint32_t temp1 = arducam_i2c_read(OV2640_CHIPID_HIGH, &vid, 1);
    uint32_t temp2 = arducam_i2c_read(OV2640_CHIPID_LOW, &pid, 1);
    info("I2C read return values: temp1=%lu temp2=%lu", (unsigned long)temp1, (unsigned long)temp2);
    (void)temp1; // only used by info(), which compiles out in RELEASE builds
    (void)temp2;
    
    if (vid != 0x26 || (pid != 0x41 && pid != 0x42)) {
        warning("Unexpected OV2640 ID: vid=0x%02X pid=0x%02X", vid, pid);
        // Don't fail hard here for now, maybe just wrong ID
    }
    
    arducam_i2c_write(0xFF, &config[0], 1);
    arducam_i2c_write(0x12, &config[1], 1); // COM7 = 0x80: sensor soft reset

    // Wait for the sensor to finish resetting before loading the register tables.
    // Without this, the JPEG/size config is written while the sensor is still
    // resetting, so the resolution never takes and the sensor stays near its
    // power-up default (~176x144) regardless of the size table below.
    //
    // NOTE: From a task, use vTaskDelay, NOT the ASF delay_ms(). delay_ms() busy-waits on
    // SysTick and overwrites SysTick->LOAD/VAL, which is the same timer FreeRTOS
    // uses for its scheduler tick -- calling it from a task corrupts the tick
    // rate and breaks every RTOS timeout. vTaskDelay yields without touching it.
    // Before the scheduler starts (the startup device check runs this from main()),
    // vTaskDelay cannot be used, so fall back to delay_ms() there.
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        delay_ms(100);
    } else {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // fmt to jpeg config
    arducam_i2c_multi_write(OV2640_JPEG_INIT);
    arducam_i2c_multi_write(OV2640_YUV422);
    arducam_i2c_multi_write(OV2640_JPEG);
    arducam_i2c_write(0xFF, &data[1], 1);
    arducam_i2c_write(0x15, &data[0], 1);
    arducam_i2c_multi_write(OV2640_800x600_JPEG); // SVGA: sharper than 320x240, a quarter the size of full-res UXGA

    return SUCCESS;
}

uint32_t arducam_i2c_write(uint8_t addr, uint8_t *data, uint16_t size) {
    uint8_t writeBuf[32 + 1];
    writeBuf[0] = addr;
    memcpy(writeBuf + 1, data, size);
    int32_t rv;
    if ((rv = io_write(arducam_i2c_io, writeBuf, size + 1)) < 0) {
        warning("Error in Arducam Write");
    }
	return rv;
}

uint32_t arducam_i2c_multi_write(const struct sensor_reg reglist[]) {
    uint8_t reg_addr = 0;
    uint8_t reg_val = 0;
    const struct sensor_reg *next = reglist;
    while ((reg_addr != 0xff) | (reg_val != 0xff)) {
        reg_addr = next->reg;
        reg_val = next->val;
        arducam_i2c_write(reg_addr, &reg_val, 1);
        next++;
    }
    return 0;
}

uint32_t arducam_i2c_read(uint8_t addr, uint8_t *readBuf, uint16_t size) {
    uint8_t writeBuf[1] = { addr };
    int32_t rv;
    if ((rv = io_write(arducam_i2c_io, writeBuf, 1)) < 0) {
        warning("Error in Arducam Write");
    }
    if ((rv = io_read(arducam_i2c_io, readBuf, size)) < 0) {
        warning("Error in Arducam Read");
    }
    return rv;
}

int32_t arducam_spi_write(uint8_t addr, uint8_t data) {
    gpio_set_pin_level(CAMERA_CS, 0);

    uint8_t value[2] = {addr | 0x80, data};  // Set MSB high for write operation
    int32_t response = io_write(arducam_spi_io, value, 2);

    gpio_set_pin_level(CAMERA_CS, 1);
    return response;
}

uint8_t arducam_spi_read(uint8_t addr) {
    gpio_set_pin_level(CAMERA_CS, 0);

    uint8_t tx_data = addr & 0x7F;  // Set MSB low for read operation
    uint8_t rx_data = 0;

    io_write(arducam_spi_io, &tx_data, 1);
    io_read(arducam_spi_io, &rx_data, 1);

    gpio_set_pin_level(CAMERA_CS, 1);
    return rx_data;
}

uint8_t get_bit(uint8_t addr, uint8_t bit) {
  uint8_t temp;
  temp = arducam_spi_read(addr);
  temp = temp & bit;
  return temp;
}

uint32_t read_fifo_length(void) {
    uint32_t len1,len2,len3,length=0;
    len1 = arducam_spi_read(FIFO_SIZE1);
    len2 = arducam_spi_read(FIFO_SIZE2);
    len3 = arducam_spi_read(FIFO_SIZE3) & 0x7f;
    length = ((len3 << 16) | (len2 << 8) | len1) & 0x07fffff;
    return length;	
}

/**
 * \fn wait_capture_done
 *
 * \brief Polls ARDUCHIP_TRIG until the capture-done flag is set, yielding between polls
 *
 * \returns `status_t`, ERROR_NOT_READY if the capture did not finish within ARDUCAM_CAPTURE_TIMEOUT_MS
 */
static status_t wait_capture_done(void) {
    // Before the scheduler starts the tick count never advances and vTaskDelay hard faults, so count 1 ms busy-waits
    const bool scheduler_running = xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
    const TickType_t start = xTaskGetTickCount();
    uint32_t busy_waited_ms = 0;
    while (!get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK)) {
        const bool timed_out = scheduler_running ? (xTaskGetTickCount() - start > pdMS_TO_TICKS(ARDUCAM_CAPTURE_TIMEOUT_MS))
                                                 : (busy_waited_ms > ARDUCAM_CAPTURE_TIMEOUT_MS);
        if (timed_out) {
            warning("Arducam capture timed out!");
            return ERROR_NOT_READY;
        }
        if (scheduler_running) {
            vTaskDelay(1);
        } else {
            delay_ms(1);
            busy_waited_ms++;
        }
    }
    return SUCCESS;
}

status_t capture(void) {
    // NOTE: In some versions of the official C++ ArduCam OV2640 driver, the FIFO_CLEAR_MASK is written twice,
    // while in others, it is written once. This implementation emprically works, so currently it is written twice.
    arducam_spi_write(ARDUCHIP_FIFO, FIFO_CLEAR_MASK); // Flush the FIFO
    arducam_spi_write(ARDUCHIP_FIFO, FIFO_CLEAR_MASK); // Clear the capture done flag (uses same clear mask)
    arducam_spi_write(ARDUCHIP_FIFO, FIFO_START_MASK); // Start capture
    ret_err_status(wait_capture_done(), "Arducam capture failed");

    // Read how many bytes were captured
    size_t len = read_fifo_length();
    if (len >= OV2640_MAX_FIFO_SIZE) {
        info("Oversized!");
        return ERROR_SANITY_CHECK_FAILED;
    } else if (len == 0 ){
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

status_t capture_rtt(void) {
    // NOTE: In some versions of the official C++ ArduCam OV2640 driver, the FIFO_CLEAR_MASK is written twice,
    // while in others, it is written once. This implementation emprically works, so currently it is written twice.
    arducam_spi_write(ARDUCHIP_FIFO, FIFO_CLEAR_MASK); // Flush the FIFO
    arducam_spi_write(ARDUCHIP_FIFO, FIFO_CLEAR_MASK); // Clear the capture done flag (uses same clear mask)
    arducam_spi_write(ARDUCHIP_FIFO, FIFO_START_MASK); // Start capture
    ret_err_status(wait_capture_done(), "Arducam capture failed");

    // Read how many bytes were captured
    size_t len = read_fifo_length();
    if (len >= OV2640_MAX_FIFO_SIZE) {
        info("Oversized!");
        return ERROR_SANITY_CHECK_FAILED;
    } else if (len == 0 ){
        info("Size is 0!");
        return ERROR_SANITY_CHECK_FAILED;
    }

    // WARNING: If you edit the info() calls, then you must also update open_rtt_channels_arducam_debug()
    // within scripts/rtt_logs.py (specifically the regex parsing), otherwise things will break.
    info("IMG_BEGIN len=%u", (unsigned)len);

    // Select SPI peripheral device
    gpio_set_pin_level(CAMERA_CS, 0);

    // Start burst read
    uint8_t cmd = BURST_FIFO_READ;
    io_write(arducam_spi_io, &cmd, 1);

    // NOTE: Do NOT clock an extra dummy byte here. After the BURST_FIFO_READ
    // command, the very next clocked byte is the first FIFO byte (the JPEG's
    // leading 0xFF). An extra io_write() would clock that byte out and discard
    // it (io_write ignores MISO), shifting the whole image left by one and
    // corrupting the SOI marker. The read loop below captures the first byte.

    // Iteratively transfer image bytes into our rx buffer (and send over RTT)
    const size_t bufferSize = ARDUCAM_SPI_RX_BUF_SIZE;
    ardu_xfer.txbuf = NULL;
    ardu_xfer.rxbuf = ardu_spi_rx_buffer;

    while (len) {
        size_t will_copy = (len < bufferSize) ? len : bufferSize;
        ardu_xfer.size = will_copy;
        spi_m_sync_transfer(&SPI_CAMERA, &ardu_xfer);
        SEGGER_RTT_Write(CAMERA_RTT_OUTPUT_CHANNEL, ardu_spi_rx_buffer, (unsigned)will_copy);
        len -= will_copy;
#ifndef BURN_SEQUENCE
        // Left out of the burn sequence: p_camera_task would pull the whole OS task list into that build
        watchdog_checkin(p_camera_task);
#endif
    }

    // Deselect SPI peripheral device
    gpio_set_pin_level(CAMERA_CS, 1);

    // WARNING: If you edit the info() calls, then you must also update open_rtt_channels_arducam_debug()
    // within scripts/rtt_logs.py (specifically the regex parsing), otherwise things will break.
    info("IMG_END");
    return SUCCESS;
}
