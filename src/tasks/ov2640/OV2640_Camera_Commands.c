/*
 * OV2640_Camera_Commands.c
 * Created: 12/29/21
 * Author: Brown Space Engineering
 */

#include "OV2640_regs.h"
#include "OV2640_sccb.h"
#include "OV2640_Camera_Commands.h"

// How many times to attempt SPI and camera detection before giving up
#define OV2640_DETECTION_ATTEMPTS 10
// The bits to send to the test register
#define OV2640_DETECTION_BITS 0x55
// Detection return values
#define OV2640_DETECTION_SUCCESS 1
#define OV2640_DETECTION_FAILURE 0

#define CS_LOW() gpio_set_pin_level(OV2640_CS, 0)
#define CS_HIGH() gpio_set_pin_level(OV2640_CS, 1)

// Private functions
uint8_t OV2640_spi_test(void);
uint8_t OV2640_module_detect(void);
void OV2640_reset_firmware(void);
void OV2640_arducam_init(void);
void OV2640_write_spi_reg(int address, int value);
uint8_t OV2640_read_spi_reg(int address);
uint8_t OV2640_spi_transceive(uint8_t send);
uint8_t OV2640_read_reg_bit(uint8_t addr, uint8_t bit);
uint32_t OV2640_read_fifo_image_buffer_length();
//void OV2640_i2c_sccb_reg_write_8_8(uint8_t reg_id, uint8_t *reg_data);
//void OV2640_i2c_sccb_reg_write_16_8(uint16_t reg_id, uint8_t *reg_data);


// TODO: status code
void OV2640_init(void) {
    // Configure CS pin
    gpio_set_pin_direction(OV2640_CS, GPIO_DIRECTION_OUT);
    gpio_set_pin_pull_mode(OV2640_CS, GPIO_PULL_OFF);
    gpio_set_pin_level(OV2640_CS, true);  // neutral high

    // Initialize Serial Camera Control Bus (SCCB)
    // TODO (I^2C awaits...)

    // Verify SPI connection and OV2640 self-identification
    uint8_t spi_success = OV2640_spi_test();
    if (!spi_success) {
        // TODO: error
    }
    // TODO: this call is used in the sample code just for identifying which
    // camera model is connected. Since we know it's always going to be the
    // OV2640, this is essentially just serving as a sanity check -- is this
    // necessary since we're already doing an SPI check above?
    uint8_t module_detect_success = OV2640_module_detect();
    if (!module_detect_success) {
        // TODO: error
    }

    // Reset firmware
    // TODO: error check
    OV2640_reset_firmware();

    // Perform camera initialization
    // TODO: error check
    OV2640_arducam_init();
}

// TODO: status code
uint32_t OV2640_capture(uint8_t *buf) {
    uint32_t i, count;
    // Flush the FIFO
    OV2640_write_spi_reg(ARDUCHIP_FIFO, FIFO_CLEAR_MASK);
    // Start capture
    OV2640_write_spi_reg(ARDUCHIP_FIFO, FIFO_START_MASK);
    // Wait for the capture to finish
    // TODO: make sure this won't stall
    while(!OV2640_read_reg_bit(ARDUCHIP_TRIG , CAP_DONE_MASK)){;}

    count = OV2640_read_fifo_image_buffer_length();
    i = 0;
    CS_LOW();
    // Enable FIFO burst mode
    OV2640_spi_transceive(BURST_FIFO_READ);
    // Read all bytes from the FIFO
    while (count--) {
        buf[i++] = OV2640_spi_transceive(0x00);
    }
    CS_HIGH();
    return i;
}

// TODO: return a status code
void OV2640_set_resolution(uint8_t resolution) {
    // TODO: check these calls
    switch (resolution) {
        case OV2640_160x120:
            OV2640_sccb_write_8bit_reg_array(OV2640_160x120_JPEG);
            break;
        case OV2640_320x240:
            OV2640_sccb_write_8bit_reg_array(OV2640_320x240_JPEG);
            break;
        case OV2640_640x480:
            OV2640_sccb_write_8bit_reg_array(OV2640_640x480_JPEG);
            break;
        case OV2640_1024x768:
            OV2640_sccb_write_8bit_reg_array(OV2640_1024x768_JPEG);
            break;
        default:
            // TODO: some sort of error?
            break;
    }
}

/**
 * Tests the SPI connection by setting the test register on the OV2640.
 * @return 1 if the test succeeds, 0 if it fails
 */
uint8_t OV2640_spi_test(void) {
    unsigned char temp;
    uint8_t attempts = 0;
    while (attempts < OV2640_DETECTION_ATTEMPTS) {
        // TODO: check these calls
        OV2640_write_spi_reg(ARDUCHIP_TEST1, OV2640_DETECTION_BITS);
        temp = OV2640_read_spi_reg(ARDUCHIP_TEST1);
        if (temp != OV2640_DETECTION_BITS) {
            // Error -- wait and try again
            attempts++;
            delay_ms(1000);
        } else {
            // Success
            return OV2640_DETECTION_SUCCESS;
        }
    }
    return OV2640_DETECTION_FAILURE;
}

/**
 * Attempts to identify the OV2640 camera module.
 * @return 1 if the test succeeds, 0 if it fails
 */
uint8_t OV2640_module_detect(void) {
    unsigned char vid, pid = 0;
    uint8_t attempts = 0;
    while (attempts < OV2640_DETECTION_ATTEMPTS) {
        // TODO: check these calls
        OV2640_sccb_write_8bit_reg(0xff, 0x01);
        OV2640_sccb_read_8bit_reg(OV2640_CHIPID_HIGH, &vid);
        OV2640_sccb_read_8bit_reg(OV2640_CHIPID_LOW, &pid);
        // TODO: this is straight from the OV2640 sample code, but clearly
        // something is awry here because ((pid != 0x41) || ( pid != 0x42)) is
        // tautological
        if ((vid != 0x26) && ((pid != 0x41) || ( pid != 0x42))) {
            attempts++;
        } else {
            return OV2640_DETECTION_SUCCESS;
        }
    }
    return OV2640_DETECTION_FAILURE;
}

// TODO: return a status code
/**
 * Performs a firmware reset on the camera (call at startup).
 */
void OV2640_reset_firmware(void) {
    // TODO: check these calls
    OV2640_write_spi_reg(0x07, 0x80);
    delay_ms(100);
    OV2640_write_spi_reg(0x07, 0x00);
    delay_ms(100);
}

// TODO: status code
/**
 * Sets the output resolution of the camera.
 * @param size One of the constants OV2640_[resolution] indicating the desired resolution.
 */
 void OV2640_set_JPEG_size(unsigned char size) {
    switch(size) {
        case OV2640_160x120:
            OV2640_sccb_write_8bit_reg_array(OV2640_160x120_JPEG);
            break;
        case OV2640_320x240:
            OV2640_sccb_write_8bit_reg_array(OV2640_320x240_JPEG);
            break;
        case OV2640_640x480:
            OV2640_sccb_write_8bit_reg_array(OV2640_640x480_JPEG);
            break;
        case OV2640_1024x768:
            OV2640_sccb_write_8bit_reg_array(OV2640_1024x768_JPEG);
            break;
        default:
            OV2640_sccb_write_8bit_reg_array(OV2640_320x240_JPEG);
            break;
    }
}

// TODO: return a status code
/**
 * Performs the ArduCAM initialization sequence, including configuring the
 * default resolution. We hardcode the image format (JPEG) and color encoding
 * (YUV422). Call at startup.
 */
void OV2640_arducam_init(void) {
    // TODO: check these calls
    // These are black-box values pulled from the ArduCAM sample code
    OV2640_sccb_write_8bit_reg(0xff, 0x01);
    OV2640_sccb_write_8bit_reg(0x12, 0x80);
    OV2640_sccb_write_8bit_reg_array(OV2640_JPEG_INIT);
    // TODO: there may be more efficient color encodings (e.g., YUV411) that we
    // may want to explore -- YUV422 is just the default used in the ArduCAM
    // sample code
    OV2640_sccb_write_8bit_reg_array(OV2640_YUV422);
    OV2640_sccb_write_8bit_reg_array(OV2640_JPEG);
    // More magic numbers from the ArduCAM code
    OV2640_sccb_write_8bit_reg(0xff, 0x01);
    OV2640_sccb_write_8bit_reg(0x15, 0x00);
    OV2640_set_JPEG_size(OV2640_DEFAULT_RESOLUTION);
}

// TODO: return a status code
/**
 * Writes an OV2640 register over SPI.
 * @param address the address to write.
 * @param value the value to write.
 */
void OV2640_write_spi_reg(int address, int value) {
    address = address | 0x80;  // set MSB 1 to indicate write
    CS_LOW();
    OV2640_spi_transceive(address);
    OV2640_spi_transceive(value);
    CS_HIGH();
}

// TODO: return a status code
/**
 * Reads an OV2640 register over SPI.
 * @param address the address to write.
 * @return the value in the specified register, or 0 if an error occurred (this
 *         should be changed to a more robust error-handling approach)
 */
uint8_t OV2640_read_spi_reg(int address) {
    uint8_t value;
    address = address & 0x7F;  // set MSB 0 to indicate read
    CS_LOW();
    OV2640_spi_transceive(address);
    value = OV2640_spi_transceive(0x00);  // dummy byte for read tick
    CS_HIGH();
    return value;
}

/**
 * Reads a specified bit from an indicated SPI register.
 * @param addr the address of the register.
 * @param bit the bit to read.
 * @return the indicated bit of the value stored at SPI register addr.
 */
uint8_t OV2640_read_reg_bit(uint8_t addr, uint8_t bit) {
    uint8_t temp;
    temp = OV2640_read_spi_reg(addr);
    temp = temp & bit;
    return temp;
}

/**
 * Returns the length of the image FIFO buffer.
 * @return the length of the buffer.
 */
uint32_t OV2640_read_fifo_image_buffer_length() {
    uint32_t len1, len2, len3, len = 0;
    len1 = OV2640_read_spi_reg(FIFO_SIZE1);
    len2 = OV2640_read_spi_reg(FIFO_SIZE2);
    len3 = OV2640_read_spi_reg(FIFO_SIZE3) & 0x7f;
    len = ((len3 << 16) | (len2 << 8) | len1) & 0x07fffff;
    return len;
}

// TODO: status code
// TODO: this should probably be made into an OS-wide SPI helper function
/**
 * Sends and receives one byte (each way) on the SPI bus.
 * @param send the byte to send.
 * @return the received byte, or 0 if an error occurred (this should be
 *         changed to a more robust error-handling approach)
 */
uint8_t OV2640_spi_transceive(uint8_t send) {
    unsigned char res;
    struct spi_xfer xfer;
    xfer.size = 1;
    xfer.txbuf = (unsigned char*) &send;
    xfer.rxbuf = &res;
    spi_m_sync_enable(&SPI_0);  // Forgetting this yields error -20
    int32_t bytes_read = spi_m_sync_transfer(&SPI_0, &xfer);
    if (bytes_read == 1) {
        return *(xfer.rxbuf);
    } else {
        // TODO: produce a helpful error
        // Somewhere account for different status codes (e.g., ERR_BUSY == -4)
        return 0x00;
    }
}
