#include <stdint.h>
#include "arducam.h"

struct spi_xfer ardu_xfer = {.rxbuf = ardu_spi_rx_buffer, .txbuf = ardu_spi_tx_buffer, .size = 0};

int32_t ARDUCAMSPIWrite(uint8_t addr, uint8_t data) {
    CS_LOW();

    uint8_t value[2] = {addr | 0x80, data};  // Set MSB high for write operation
    int32_t response = io_write(arducam_spi_io, value, 2);

    CS_HIGH();
    return response;
}

int8_t ARDUCAMSPIRead(uint8_t addr) {
    CS_LOW();

    uint8_t tx_data = addr & 0x7F;  // Set MSB low for read operation
    uint8_t rx_data = 0;

    io_write(arducam_spi_io, &tx_data, 1);
    io_read(arducam_spi_io, &rx_data, 1);

    CS_HIGH();
    return rx_data;
}
