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
#include "jpeg_restart.h"
#include "mram_map.h"
#include "mutexes.h"
#include "os_delay.h"
#include "task_list.h"
#include "watchdog_task.h"

// Buffer for SPI transactions
uint8_t ardu_spi_rx_buffer[ARDUCAM_SPI_RX_BUF_SIZE] = {0x00};
uint8_t camera_rtt_image_buffer[SEGGER_RTT_IMAGE_BUFFER_SIZE];
uint8_t ardu_spi_tx_buffer[ARDUCAM_SPI_TX_BUF_SIZE] = {0x00};
struct spi_xfer ardu_xfer = {.rxbuf = ardu_spi_rx_buffer, .txbuf = ardu_spi_tx_buffer, .size = 0};

_Static_assert(OV2640_MAX_FIFO_SIZE <= ARDUCAM_MRAM_IMAGE_SIZE, "a capture must fit its MRAM buffer");
_Static_assert(ARDUCAM_MRAM_IMAGE_INFO_OFFSET + MRAM_RECORD_OVERHEAD + sizeof(arducam_mram_image_t) <= ARDUCAM_MRAM_IMAGE_OFFSET,
               "the image info record runs into the image");
_Static_assert(ARDUCAM_MRAM_IMAGE_OFFSET + ARDUCAM_MRAM_IMAGE_SIZE <= MRAM_CAMERA_SIZE, "the image must fit its MRAM region");

struct io_descriptor *arducam_i2c_io;
struct io_descriptor *arducam_spi_io;

// Undo arducam_sleep()'s ArduChip low-power mode and sensor PWDN
static void arducam_wake(void) {
    const uint8_t tim = arducam_spi_read(ARDUCHIP_TIM);
    const uint8_t gpio = arducam_spi_read(ARDUCHIP_GPIO);
    if (!(tim & LOW_POWER_MODE) && !(gpio & GPIO_PWDN_MASK)) {
        return; // Not asleep, e.g. at power-up
    }
    arducam_spi_write(ARDUCHIP_TIM, (uint8_t)(tim & ~LOW_POWER_MODE));
    arducam_spi_write(ARDUCHIP_GPIO, (uint8_t)(gpio & ~GPIO_PWDN_MASK));
    os_delay_ms(ARDUCAM_WAKE_DELAY_MS);
}

static const struct sensor_reg *const resolution_tables[ARDUCAM_RESOLUTION_COUNT] = {
    [ARDUCAM_160x120] = OV2640_160x120_JPEG,   [ARDUCAM_176x144] = OV2640_176x144_JPEG,
    [ARDUCAM_320x240] = OV2640_320x240_JPEG,   [ARDUCAM_352x288] = OV2640_352x288_JPEG,
    [ARDUCAM_640x480] = OV2640_640x480_JPEG,   [ARDUCAM_800x600] = OV2640_800x600_JPEG,
    [ARDUCAM_1024x768] = OV2640_1024x768_JPEG, [ARDUCAM_1280x960] = OV2640_1280x960_JPEG,
    [ARDUCAM_1600x1200] = OV2640_1600x1200_JPEG,
};

/**
 * \fn init_arducam_hardware
 *
 * \brief Initializes the arducam hardware, waking it first if arducam_sleep() put it to sleep
 *
 * \param resolution the size of the JPEGs that captures produce until the next init
 *
 * \returns `status_t`, whether the operation was successful; ERROR_BAD_ARGS for an unknown resolution
 */
status_t init_arducam_hardware(arducam_resolution_t resolution) {
    if ((uint32_t)resolution >= ARDUCAM_RESOLUTION_COUNT) {
        warning("Unknown camera resolution %d\n", (int)resolution);
        return ERROR_BAD_ARGS;
    }

    // main() enables I2C_SENSORS; this only needs its io descriptor
    i2c_m_sync_get_io_descriptor(&I2C_SENSORS, &arducam_i2c_io);

    gpio_set_pin_direction(CAMERA_CS, GPIO_DIRECTION_OUT);
    gpio_set_pin_level(CAMERA_CS, 1);

    // spi_m_sync_set_baudrate() takes the BAUD register value, not a frequency (SAMD51 datasheet 35.6.2.3), and only
    // while the SPI is disabled
    spi_m_sync_disable(&SPI_CAMERA);
    spi_m_sync_set_baudrate(&SPI_CAMERA, CONF_GCLK_SERCOM2_CORE_FREQUENCY / (2 * ARDUCAM_SPI_HZ) - 1);
    spi_m_sync_get_io_descriptor(&SPI_CAMERA, &arducam_spi_io);
    spi_m_sync_enable(&SPI_CAMERA);
    arducam_wake(); // Before anything else, so that low-power mode can't make the checks below fail
    
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
    if ((int32_t)temp1 < 0 || (int32_t)temp2 < 0) {
        // Don't go on to write hundreds of registers into a bus that isn't answering: each write waits out the I2C
        // timeout, and together they outlast the watchdog
        warning("Camera I2C not answering (%ld, %ld)\n", (long)(int32_t)temp1, (long)(int32_t)temp2);
        return ERROR_I2C_FAILED;
    }
    
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
    os_delay_ms(100);

    // fmt to jpeg config
    arducam_i2c_multi_write(OV2640_JPEG_INIT);
    arducam_i2c_multi_write(OV2640_YUV422);
    arducam_i2c_multi_write(OV2640_JPEG);
    arducam_i2c_write(0xFF, &data[1], 1);
    arducam_i2c_write(0x15, &data[0], 1);
    arducam_i2c_multi_write(resolution_tables[resolution]);

    // The soft reset above restarts auto-exposure: a capture right away comes out black (measured mean brightness
    // 3/255), one ARDUCAM_EXPOSURE_SETTLE_MS later it's exposed normally (43/255)
    os_delay_ms(ARDUCAM_EXPOSURE_SETTLE_MS);

    return SUCCESS;
}

/**
 * \fn arducam_sleep
 *
 * \brief Puts the camera in its lowest-power state in two ways, since which ones work depends on the board revision:
 *      the sensor's PWDN pin and the ArduChip's low-power mode. The FIFO contents are lost. init_arducam_hardware()
 *      wakes it again.
 *
 * \warning Not the sensor's software standby (COM2 bit 4): on our board the sensor holds SDA low once it enters it,
 *      which takes the whole shared I2C bus down until the sensor is reset.
 */
void arducam_sleep(void) {
    // Hold PWDN high. Only bit 1: as outputs driven low, bits 0 and 2 would hold the sensor in reset or cut its power.
    // The value goes first so the pin is already high when it becomes an output.
    arducam_spi_write(ARDUCHIP_GPIO, arducam_spi_read(ARDUCHIP_GPIO) | GPIO_PWDN_MASK);
    arducam_spi_write(ARDUCHIP_GPIO_DIR, arducam_spi_read(ARDUCHIP_GPIO_DIR) | GPIO_PWDN_MASK);

    // Last, the ArduChip's own low-power mode, which the other bits of ARDUCHIP_TIM must survive
    arducam_spi_write(ARDUCHIP_TIM, arducam_spi_read(ARDUCHIP_TIM) | LOW_POWER_MODE);
}

uint32_t arducam_i2c_write(uint8_t addr, uint8_t *data, uint16_t size) {
    uint8_t writeBuf[32 + 1];
    writeBuf[0] = addr;
    memcpy(writeBuf + 1, data, size);
    int32_t rv;
    lock_mutex(i2c_sensors_bus_mutex);
    // The slave address is per descriptor, and the other drivers on this bus change it
    i2c_m_sync_set_slaveaddr(&I2C_SENSORS, ARDUCAM_ADDR >> 1, I2C_M_SEVEN);
    rv = io_write(arducam_i2c_io, writeBuf, size + 1);
    unlock_mutex(i2c_sensors_bus_mutex);
    if (rv < 0) {
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
    // Held across the address write and the read so no other transfer lands between them
    lock_mutex(i2c_sensors_bus_mutex);
    i2c_m_sync_set_slaveaddr(&I2C_SENSORS, ARDUCAM_ADDR >> 1, I2C_M_SEVEN);
    if ((rv = io_write(arducam_i2c_io, writeBuf, 1)) < 0) {
        warning("Error in Arducam Write");
    }
    if ((rv = io_read(arducam_i2c_io, readBuf, size)) < 0) {
        warning("Error in Arducam Read");
    }
    unlock_mutex(i2c_sensors_bus_mutex);
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
    // Each os_delay_ms(1) lasts at least 1 ms, so counting them never times out early
    uint32_t waited_ms = 0;
    while (!get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK)) {
        if (waited_ms > ARDUCAM_CAPTURE_TIMEOUT_MS) {
            warning("Arducam capture timed out!");
            return ERROR_NOT_READY;
        }
        os_delay_ms(1);
        waited_ms++;
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

// Flushes the FIFO, captures one frame into it, and sets *p_len to its length in bytes
static status_t capture_to_fifo(size_t *p_len) {
    // NOTE: In some versions of the official C++ ArduCam OV2640 driver, the FIFO_CLEAR_MASK is written twice,
    // while in others, it is written once. This implementation emprically works, so currently it is written twice.
    arducam_spi_write(ARDUCHIP_FIFO, FIFO_CLEAR_MASK); // Flush the FIFO
    arducam_spi_write(ARDUCHIP_FIFO, FIFO_CLEAR_MASK); // Clear the capture done flag (uses same clear mask)
    arducam_spi_write(ARDUCHIP_FIFO, FIFO_START_MASK); // Start capture
    ret_err_status(wait_capture_done(), "Arducam capture failed");

    // Read how many bytes were captured
    *p_len = read_fifo_length();
    if (*p_len >= OV2640_MAX_FIFO_SIZE) {
        info("Oversized!");
        return ERROR_SANITY_CHECK_FAILED;
    } else if (*p_len == 0) {
        info("Size is 0!");
        return ERROR_SANITY_CHECK_FAILED;
    }
    return SUCCESS;
}

// Reads the captured frame from the FIFO in a burst. Only once: FIFO_RDPTR_RST_MASK doesn't rewind our ArduChip.
typedef struct {
    size_t remaining;
} fifo_reader_t;

static void fifo_read_begin(fifo_reader_t *p_reader, size_t len) {
    p_reader->remaining = len;
    gpio_set_pin_level(CAMERA_CS, 0);

    // NOTE: Do NOT clock an extra dummy byte after this. After the BURST_FIFO_READ
    // command, the very next clocked byte is the first FIFO byte (the JPEG's
    // leading 0xFF). An extra io_write() would clock that byte out and discard
    // it (io_write ignores MISO), shifting the whole image left by one and
    // corrupting the SOI marker.
    uint8_t cmd = BURST_FIFO_READ;
    io_write(arducam_spi_io, &cmd, 1);
}

static void fifo_read_end(void) {
    gpio_set_pin_level(CAMERA_CS, 1);
}

// A jpeg_restart_read_fn
static size_t fifo_read(void *ctx, uint8_t *buf, size_t cap) {
    fifo_reader_t *p_reader = ctx;
    const size_t n = p_reader->remaining < cap ? p_reader->remaining : cap;
    if (n) {
        ardu_xfer.txbuf = NULL;
        ardu_xfer.rxbuf = buf;
        ardu_xfer.size = n;
        spi_m_sync_transfer(&SPI_CAMERA, &ardu_xfer);
        p_reader->remaining -= n;
#ifndef BURN_SEQUENCE
        // Left out of the burn sequence: p_camera_task would pull the whole OS task list into that build
        watchdog_checkin(p_camera_task);
#endif
    }
    return n;
}

// jpeg_restart_write_fns
static bool write_rtt(void *ctx, const uint8_t *buf, size_t len) {
    (void)ctx;
    SEGGER_RTT_Write(CAMERA_RTT_OUTPUT_CHANNEL, buf, (unsigned)len);
    return true;
}

typedef struct {
    size_t offset;
} mram_writer_t;

static bool write_mram(void *ctx, const uint8_t *buf, size_t len) {
    mram_writer_t *p_writer = ctx;
    if (p_writer->offset + len > ARDUCAM_MRAM_IMAGE_SIZE ||
        !mram_region_write(MRAM_REGION_CAMERA, ARDUCAM_MRAM_IMAGE_OFFSET + p_writer->offset, buf, len)) {
        return false;
    }
    p_writer->offset += len;
    return true;
}

/**
 * \fn capture_rtt
 *
 * \brief Captures a JPEG and streams it over RTT channel CAMERA_RTT_OUTPUT_CHANNEL with restart markers added (see
 *      jpeg_restart.h), framed by IMG_BEGIN and IMG_END log lines. The length comes at the end, in IMG_END, since it
 *      isn't known until the whole frame has been through.
 *
 * \returns `status_t`, whether the operation was successful
 */
status_t capture_rtt(void) {
    size_t len = 0;
    ret_err_status(capture_to_fifo(&len), "Arducam capture failed");

    // WARNING: If you edit the info() calls, then you must also update open_rtt_channels_arducam_debug()
    // within scripts/rtt_logs.py (specifically the regex parsing), otherwise things will break.
    info("IMG_BEGIN");

    fifo_reader_t reader;
    size_t out_len = 0;
    fifo_read_begin(&reader, len);
    const jpeg_restart_result_t result = jpeg_add_restart_markers(fifo_read, &reader, write_rtt, NULL, &out_len);
    fifo_read_end();

    // WARNING: If you edit the info() calls, then you must also update open_rtt_channels_arducam_debug()
    // within scripts/rtt_logs.py (specifically the regex parsing), otherwise things will break.
    info("IMG_END len=%u", (unsigned)out_len);
    if (result != JPEG_RESTART_OK) {
        warning("camera: Couldn't add restart markers (%d); the image sent is incomplete\n", result);
        return ERROR_SANITY_CHECK_FAILED;
    }
    return SUCCESS;
}

/**
 * \fn capture_mram
 *
 * \brief Captures a JPEG and streams it from the camera's FIFO into MRAM_REGION_CAMERA with restart markers added (see
 *      jpeg_restart.h), replacing the last one. The image's length is saved only once the whole image is in MRAM, so a
 *      failed capture leaves no image rather than a partial one.
 *
 * \returns `status_t`, whether the operation was successful
 */
status_t capture_mram(void) {
    size_t len = 0;
    ret_err_status(capture_to_fifo(&len), "Arducam capture failed");

    // The old image is overwritten from here on
    arducam_mram_image_t image = {.length = 0};
    if (!mram_record_save(MRAM_REGION_CAMERA, ARDUCAM_MRAM_IMAGE_INFO_OFFSET, &image, sizeof(image))) {
        warning("camera: Failed to clear the MRAM image info\n");
        return ERROR_WRITE_FAILED;
    }

    fifo_reader_t reader;
    mram_writer_t writer = {.offset = 0};
    size_t out_len = 0;
    fifo_read_begin(&reader, len);
    const jpeg_restart_result_t result = jpeg_add_restart_markers(fifo_read, &reader, write_mram, &writer, &out_len);
    fifo_read_end();
    if (result == JPEG_RESTART_WRITE_FAILED) {
        warning("camera: Failed to write the image to MRAM\n");
        return ERROR_WRITE_FAILED;
    } else if (result != JPEG_RESTART_OK) {
        warning("camera: Couldn't add restart markers (%d)\n", result);
        return ERROR_SANITY_CHECK_FAILED;
    }

    image.length = out_len;
    if (!mram_record_save(MRAM_REGION_CAMERA, ARDUCAM_MRAM_IMAGE_INFO_OFFSET, &image, sizeof(image))) {
        warning("camera: Failed to save the MRAM image info\n");
        return ERROR_WRITE_FAILED;
    }
    info("camera: %u-byte image saved to MRAM\n", (unsigned)out_len);
    return SUCCESS;
}

/**
 * \fn arducam_mram_image_length
 *
 * \brief Finds the length of the image capture_mram() last saved. Its bytes can be read with mram_region_read() from
 *      MRAM_REGION_CAMERA, starting at ARDUCAM_MRAM_IMAGE_OFFSET.
 *
 * \param p_length set to the image's length in bytes
 *
 * \returns `status_t`, ERROR_READ_FAILED if there is no complete image in MRAM
 */
status_t arducam_mram_image_length(uint32_t *p_length) {
    arducam_mram_image_t image;
    if (!mram_record_load(MRAM_REGION_CAMERA, ARDUCAM_MRAM_IMAGE_INFO_OFFSET, &image, sizeof(image)) || image.length == 0 ||
        image.length > ARDUCAM_MRAM_IMAGE_SIZE) {
        return ERROR_READ_FAILED;
    }
    *p_length = image.length;
    return SUCCESS;
}
