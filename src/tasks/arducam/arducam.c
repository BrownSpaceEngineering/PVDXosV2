/**
 * Code for the Arducam
 * 
 * Created: Nov 17, 2024 4:26 AM
 * By: Alexander Thaep
*/

// Essential resource: https://github.com/ArduCAM/Arduino

#include "arducam.h"
#include "string.h"
#include "logging.h"

#define I2C_SERCOM       SERCOM6

struct arducamTaskMemory arducamMem;

uint8_t ardu_spi_rx_buffer[ARDUCAM_SPI_BUFFER_CAPACITY] = { 0x00 };
uint8_t ardu_spi_tx_buffer[ARDUCAM_SPI_BUFFER_CAPACITY] = { 0x00 };

struct io_descriptor *arducam_i2c_io;
struct io_descriptor *arducam_spi_io;

const struct sensor_reg OV2640_JPEG_INIT[] =
{
  { 0xff, 0x00 },
  { 0x2c, 0xff },
  { 0x2e, 0xdf },
  { 0xff, 0x01 },
  { 0x3c, 0x32 },
  { 0x11, 0x00 },	
  { 0x09, 0x02 },
  { 0x04, 0x28 },
  { 0x13, 0xe5 },
  { 0x14, 0x48 },
  { 0x2c, 0x0c },
  { 0x33, 0x78 },
  { 0x3a, 0x33 },
  { 0x3b, 0xfB },
  { 0x3e, 0x00 },
  { 0x43, 0x11 },
  { 0x16, 0x10 },
  { 0x39, 0x92 },
  { 0x35, 0xda },
  { 0x22, 0x1a },
  { 0x37, 0xc3 },
  { 0x23, 0x00 },
  { 0x34, 0xc0 },
  { 0x36, 0x1a },
  { 0x06, 0x88 },
  { 0x07, 0xc0 },
  { 0x0d, 0x87 },
  { 0x0e, 0x41 },
  { 0x4c, 0x00 },
  { 0x48, 0x00 },
  { 0x5B, 0x00 },
  { 0x42, 0x03 },
  { 0x4a, 0x81 },
  { 0x21, 0x99 },
  { 0x24, 0x40 },
  { 0x25, 0x38 },
  { 0x26, 0x82 },
  { 0x5c, 0x00 },
  { 0x63, 0x00 },
  { 0x61, 0x70 },
  { 0x62, 0x80 },
  { 0x7c, 0x05 },
  { 0x20, 0x80 },
  { 0x28, 0x30 },
  { 0x6c, 0x00 },
  { 0x6d, 0x80 },
  { 0x6e, 0x00 },
  { 0x70, 0x02 },
  { 0x71, 0x94 },
  { 0x73, 0xc1 },
  { 0x12, 0x40 },
  { 0x17, 0x11 },
  { 0x18, 0x43 },
  { 0x19, 0x00 },
  { 0x1a, 0x4b },
  { 0x32, 0x09 },
  { 0x37, 0xc0 },
  { 0x4f, 0x60 },
  { 0x50, 0xa8 },
  { 0x6d, 0x00 },
  { 0x3d, 0x38 },
  { 0x46, 0x3f },
  { 0x4f, 0x60 },
  { 0x0c, 0x3c },
  { 0xff, 0x00 },
  { 0xe5, 0x7f },
  { 0xf9, 0xc0 },
  { 0x41, 0x24 },
  { 0xe0, 0x14 },
  { 0x76, 0xff },
  { 0x33, 0xa0 },
  { 0x42, 0x20 },
  { 0x43, 0x18 },
  { 0x4c, 0x00 },
  { 0x87, 0xd5 },
  { 0x88, 0x3f },
  { 0xd7, 0x03 },
  { 0xd9, 0x10 },
  { 0xd3, 0x82 },
  { 0xc8, 0x08 },
  { 0xc9, 0x80 },
  { 0x7c, 0x00 },
  { 0x7d, 0x00 },
  { 0x7c, 0x03 },
  { 0x7d, 0x48 },
  { 0x7d, 0x48 },
  { 0x7c, 0x08 },
  { 0x7d, 0x20 },
  { 0x7d, 0x10 },
  { 0x7d, 0x0e },
  { 0x90, 0x00 },
  { 0x91, 0x0e },
  { 0x91, 0x1a },
  { 0x91, 0x31 },
  { 0x91, 0x5a },
  { 0x91, 0x69 },
  { 0x91, 0x75 },
  { 0x91, 0x7e },
  { 0x91, 0x88 },
  { 0x91, 0x8f },
  { 0x91, 0x96 },
  { 0x91, 0xa3 },
  { 0x91, 0xaf },
  { 0x91, 0xc4 },
  { 0x91, 0xd7 },
  { 0x91, 0xe8 },
  { 0x91, 0x20 },
  { 0x92, 0x00 },
  { 0x93, 0x06 },
  { 0x93, 0xe3 },
  { 0x93, 0x05 },
  { 0x93, 0x05 },
  { 0x93, 0x00 },
  { 0x93, 0x04 },
  { 0x93, 0x00 },
  { 0x93, 0x00 },
  { 0x93, 0x00 },
  { 0x93, 0x00 },
  { 0x93, 0x00 },
  { 0x93, 0x00 },
  { 0x93, 0x00 },
  { 0x96, 0x00 },
  { 0x97, 0x08 },
  { 0x97, 0x19 },
  { 0x97, 0x02 },
  { 0x97, 0x0c },
  { 0x97, 0x24 },
  { 0x97, 0x30 },
  { 0x97, 0x28 },
  { 0x97, 0x26 },
  { 0x97, 0x02 },
  { 0x97, 0x98 },
  { 0x97, 0x80 },
  { 0x97, 0x00 },
  { 0x97, 0x00 },
  { 0xc3, 0xed },
  { 0xa4, 0x00 },
  { 0xa8, 0x00 },
  { 0xc5, 0x11 },
  { 0xc6, 0x51 },
  { 0xbf, 0x80 },
  { 0xc7, 0x10 },
  { 0xb6, 0x66 },
  { 0xb8, 0xA5 },
  { 0xb7, 0x64 },
  { 0xb9, 0x7C },
  { 0xb3, 0xaf },
  { 0xb4, 0x97 },
  { 0xb5, 0xFF },
  { 0xb0, 0xC5 },
  { 0xb1, 0x94 },
  { 0xb2, 0x0f },
  { 0xc4, 0x5c },
  { 0xc0, 0x64 },
  { 0xc1, 0x4B },
  { 0x8c, 0x00 },
  { 0x86, 0x3D },
  { 0x50, 0x00 },
  { 0x51, 0xC8 },
  { 0x52, 0x96 },
  { 0x53, 0x00 },
  { 0x54, 0x00 },
  { 0x55, 0x00 },
  { 0x5a, 0xC8 },
  { 0x5b, 0x96 },
  { 0x5c, 0x00 },
  { 0xd3, 0x00 },	//{ 0xd3, 0x7f },
  { 0xc3, 0xed },
  { 0x7f, 0x00 },
  { 0xda, 0x00 },
  { 0xe5, 0x1f },
  { 0xe1, 0x67 },
  { 0xe0, 0x00 },
  { 0xdd, 0x7f },
  { 0x05, 0x00 },

  { 0x12, 0x40 },
  { 0xd3, 0x04 },	//{ 0xd3, 0x7f },
  { 0xc0, 0x16 },
  { 0xC1, 0x12 },
  { 0x8c, 0x00 },
  { 0x86, 0x3d },
  { 0x50, 0x00 },
  { 0x51, 0x2C },
  { 0x52, 0x24 },
  { 0x53, 0x00 },
  { 0x54, 0x00 },
  { 0x55, 0x00 },
  { 0x5A, 0x2c },
  { 0x5b, 0x24 },
  { 0x5c, 0x00 },
  { 0xff, 0xff },
};             

const struct sensor_reg OV2640_YUV422[] =
{
  { 0xFF, 0x00 },
  { 0x05, 0x00 },
  { 0xDA, 0x10 },
  { 0xD7, 0x03 },
  { 0xDF, 0x00 },
  { 0x33, 0x80 },
  { 0x3C, 0x40 },
  { 0xe1, 0x77 },
  { 0x00, 0x00 },
  { 0xff, 0xff },
};

const struct sensor_reg OV2640_JPEG[] =  
{
  { 0xe0, 0x14 },
  { 0xe1, 0x77 },
  { 0xe5, 0x1f },
  { 0xd7, 0x03 },
  { 0xda, 0x10 },
  { 0xe0, 0x00 },
  { 0xFF, 0x01 },
  { 0x04, 0x08 },
  { 0xff, 0xff },
}; 

const struct sensor_reg OV2640_320x240_JPEG[] =  
{
  { 0xff, 0x01 },
  { 0x12, 0x40 },
  { 0x17, 0x11 },
  { 0x18, 0x43 },
  { 0x19, 0x00 },
  { 0x1a, 0x4b },
  { 0x32, 0x09 },
  { 0x4f, 0xca },
  { 0x50, 0xa8 },
  { 0x5a, 0x23 },
  { 0x6d, 0x00 },
  { 0x39, 0x12 },
  { 0x35, 0xda },
  { 0x22, 0x1a },
  { 0x37, 0xc3 },
  { 0x23, 0x00 },
  { 0x34, 0xc0 },
  { 0x36, 0x1a },
  { 0x06, 0x88 },
  { 0x07, 0xc0 },
  { 0x0d, 0x87 },
  { 0x0e, 0x41 },
  { 0x4c, 0x00 },
  { 0xff, 0x00 },
  { 0xe0, 0x04 },
  { 0xc0, 0x64 },
  { 0xc1, 0x4b },
  { 0x86, 0x35 },
  { 0x50, 0x89 },
  { 0x51, 0xc8 },
  { 0x52, 0x96 },
  { 0x53, 0x00 },
  { 0x54, 0x00 },
  { 0x55, 0x00 },
  { 0x57, 0x00 },
  { 0x5a, 0x50 },
  { 0x5b, 0x3c },
  { 0x5c, 0x00 },
  { 0xe0, 0x00 },
  { 0xff, 0xff },
};

void init_arducam()
{
    i2c_m_sync_set_baudrate(&I2C_0, 0, 115200);
    i2c_m_sync_get_io_descriptor(&I2C_0, &arducam_i2c_io);
    i2c_m_sync_enable(&I2C_0);
    i2c_m_sync_set_slaveaddr(&I2C_0, ARDUCAMAddress >> 1, I2C_M_SEVEN);

    watchdog_checkin(ARDUCAM_TASK);

    spi_m_sync_set_baudrate(&SPI_0, 4000000);
    spi_m_sync_get_io_descriptor(&SPI_0, &arducam_spi_io);
    spi_m_sync_enable(&SPI_0);

    uint8_t vidpid[2] = { 0, 0 };
    uint8_t data[2] = { 0x00, 0x01 };

    // ADD TEST CODE HERE
    ARDUCAMwReg(ARDUCHIP_TEST1, 0x55);
    int temp = ARDUCAMrReg(ARDUCHIP_TEST1);
    if (temp != 0x55){
        while(1);
    }

    ARDUCAMI2CWrite( 0xFF, data + 1, 1 );
    ARDUCAMI2CRead( OV2640_CHIPID_HIGH, vidpid, 1 );
    ARDUCAMI2CRead( OV2640_CHIPID_LOW, vidpid + 1, 1 );

    wrSensorRegs8_8(OV2640_JPEG_INIT);
    wrSensorRegs8_8(OV2640_YUV422);
    wrSensorRegs8_8(OV2640_JPEG);

    ARDUCAMI2CWrite( 0xFF, data + 1, 1 );
    ARDUCAMI2CWrite( 0x15, data, 1);

    wrSensorRegs8_8(OV2640_320x240_JPEG);

    ardu_xfer.size = 2;
    ardu_spi_tx_buffer[0] = ARDUCHIP_FIFO;
    ardu_spi_tx_buffer[1] = FIFO_CLEAR_MASK;
    arducam_spi_write_command();

    if ((vidpid[0] == 0x26 ))
    {
        info("vid: %d", vidpid[0]);
        info("pid: %d", vidpid[1]);
    }

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

// Write the contents of spi_tx_buffer to the display as a command
int32_t arducam_spi_write_command() {
    CS_LOW(); // select the display for SPI communication

    int32_t response = spi_m_sync_transfer(&SPI_0, &ardu_xfer);
    if (response != (int32_t)ardu_xfer.size) {
        return -1;
    }

    CS_HIGH(); // deselect the display for SPI communication
    return response;
}

uint32_t ARDUCAMI2CWrite(uint8_t addr, uint8_t *data, uint16_t size)
{
    uint8_t writeBuf[32 + 1];
    writeBuf[0] = addr;
    memcpy(&(writeBuf[1]), data, size);
    int32_t rv;
    if ((rv = io_write(arducam_i2c_io, writeBuf, size + 1)) < 0){
        warning("Error in Arducam Write");
    }
	return rv;
}

uint32_t wrSensorRegs8_8(const struct sensor_reg reglist[])
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
