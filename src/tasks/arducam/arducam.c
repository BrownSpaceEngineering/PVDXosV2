/**
 * Code for the Arducam
 * 
 * Created: Nov 17, 2024 4:26 AM
 * By: Alexander Thaep, Tanish Makadia, Zach Mahan
*/

// Essential resource: https://github.com/ArduCAM/Arduino

#include "arducam.h"
#include "arducam_registers.h"
#include "string.h"
#include "logging.h"

struct io_descriptor *arducam_i2c_io;
struct io_descriptor *arducam_spi_io;
struct arducamTaskMemory arducamMem;

uint8_t ardu_spi_rx_buffer[ARDUCAM_SPI_RX_BUF_SIZE] = { 0x00 };
uint8_t ardu_spi_tx_buffer[ARDUCAM_SPI_TX_BUF_SIZE] = { 0x00 };

void init_arducam()
{
    // Configure ASF I2C and SPI drivers for this device
    i2c_m_sync_set_baudrate(&I2C_0, 0, 100000);
    i2c_m_sync_get_io_descriptor(&I2C_0, &arducam_i2c_io);
    i2c_m_sync_enable(&I2C_0);
    i2c_m_sync_set_slaveaddr(&I2C_0, ARDUCAM_ADDR >> 1, I2C_M_SEVEN);

    watchdog_checkin(ARDUCAM_TASK);

    gpio_set_pin_direction(Camera_CS, GPIO_DIRECTION_OUT);
    gpio_set_pin_level(Camera_CS, 1);

    spi_m_sync_set_baudrate(&SPI_0, 6000000);
    spi_m_sync_get_io_descriptor(&SPI_0, &arducam_spi_io);
    spi_m_sync_enable(&SPI_0);
    
    // Write and read a known test pattern (0x55) into ArduCHIP internal test register
    ARDUCAMSPIWrite(ARDUCHIP_TEST1, 0x55);
    int temp = ARDUCAMSPIRead(ARDUCHIP_TEST1);
    if (temp != 0x55){
        warning("Camera SPI interface error!");
    }

    // Save payload bytes as variables so we can pass by reference
    uint8_t data[2] = { 0x00, 0x01 };
    uint8_t config[2] = { 0x01, 0x80 };
    
    // Verify that the camera sensor is an OV2640 based on VID and PID
    uint8_t vid = 0, pid = 0;
    ARDUCAMI2CWrite(0xFF, &data[1], 1);
    uint32_t temp1 = ARDUCAMI2CRead(OV2640_CHIPID_HIGH, &vid, 1);
    uint32_t temp2 = ARDUCAMI2CRead(OV2640_CHIPID_LOW, &pid, 1);
    info("I2C read return values: temp1=%lu temp2=%lu", (unsigned long)temp1, (unsigned long)temp2);
    
    if (vid != 0x26 || (pid != 0x41 && pid != 0x42)) {
        info("Unexpected OV2640 ID: vid=0x%02X pid=0x%02X", vid, pid);
    }
    
    ARDUCAMI2CWrite(0xFF, &config[0], 1);
    ARDUCAMI2CWrite(0x12, &config[1], 1);

    // fmt to jpeg config
    ARDUCAMI2CMultiWrite(OV2640_JPEG_INIT);
    ARDUCAMI2CMultiWrite(OV2640_YUV422);
    ARDUCAMI2CMultiWrite(OV2640_JPEG);
    ARDUCAMI2CWrite(0xFF, &data[1], 1);
    ARDUCAMI2CWrite(0x15, &data[0], 1);
    ARDUCAMI2CMultiWrite(OV2640_1280x1024_JPEG);

    // TODO: revert this back to capture() when radio is working
    // capture();
    capture_rtt();

    watchdog_checkin(ARDUCAM_TASK);

    return;
}

void arducam_main(void *pvParameters) {
    info("Arducam Task Started!\r\n");

    init_arducam();

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        watchdog_checkin(ARDUCAM_TASK);
    }

    return;
}

uint32_t ARDUCAMI2CWrite(uint8_t addr, uint8_t *data, uint16_t size)
{
    uint8_t writeBuf[32 + 1];
    writeBuf[0] = addr;
    memcpy(writeBuf + 1, data, size);
    int32_t rv;
    if ((rv = io_write(arducam_i2c_io, writeBuf, size + 1)) < 0){
        warning("Error in Arducam Write");
    }
	return rv;
}

uint32_t ARDUCAMI2CMultiWrite(const struct sensor_reg reglist[])
{
    uint8_t reg_addr = 0;
    uint8_t reg_val = 0;
    const struct sensor_reg *next = reglist;
    while ((reg_addr != 0xff) | (reg_val != 0xff))
    {
        reg_addr = next->reg;
        reg_val = next->val;
        ARDUCAMI2CWrite(reg_addr, &reg_val, 1);
        next++;
    }
    return 0;
}

uint32_t ARDUCAMI2CRead(uint8_t addr, uint8_t *readBuf, uint16_t size) 
{
    uint8_t writeBuf[1] = { addr };
    int32_t rv;
    if ((rv = io_write(arducam_i2c_io, writeBuf, 1)) < 0){
        warning("Error in Arducam Write");
    }
    if ((rv = io_read(arducam_i2c_io, readBuf, size)) < 0) {
        warning("Error in Arducam Read");
    }
    return rv;
}