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

struct io_descriptor *arducam_i2c_io;
struct io_descriptor *arducam_spi_io;

static Format cformat;

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

    cformat = JPEG;

    uint8_t vidpid[2] = { 0, 0 };
    uint8_t data[1] = { 0x01 };

    ARDUCAMI2CWrite( 0xFF, data, 1 );
    ARDUCAMI2CRead( OV2640_CHIPID_HIGH, vidpid, 1 );
    ARDUCAMI2CRead( OV2640_CHIPID_LOW, vidpid + 1, 1 );

    if (vidpid[0] == 0)
    {
        info("vidpid: %d", vidpid[0]);
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

uint32_t ARDUCAMI2CWrite(uint8_t addr, uint8_t *data, uint16_t size)
{
    uint8_t writeBuf[32 + 1];
    writeBuf[0] = addr;
    memcpy(&(writeBuf[1]), data, size);
    int32_t rv;
    if ((rv = io_write(arducam_spi_io, writeBuf, size + 1)) < 0){
        warning("Error in Arducam Write");
    }
	return rv;
}

int32_t ARDUCAMI2CRead(uint8_t addr, uint8_t *readBuf, uint16_t size) 
{
    uint8_t writeBuf[1] = { addr };
    int32_t rv;
    if ((rv = io_write(arducam_spi_io, writeBuf, 1)) < 0){
        warning("Error in Arducam Write");
    }
    if ((rv = io_read(arducam_spi_io, readBuf, size)) < 0) {
        warning("Error in Arducam Read");
    }
    return rv;
}