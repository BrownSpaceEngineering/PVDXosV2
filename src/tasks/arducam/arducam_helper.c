#include <stdint.h>
#include "arducam.h"

struct spi_xfer ardu_xfer = {.rxbuf = ardu_spi_rx_buffer, .txbuf = ardu_spi_tx_buffer, .size = 0};

int32_t ARDUCAMSPIWrite(uint8_t addr, uint8_t data) {
    CS_LOW();

    uint8_t tx[] = { addr, data };

    int32_t response = io_write(arducam_spi_io, tx, sizeof(tx));
    if(response != (int32_t) sizeof(tx)) {
        return -1;
    }

    CS_HIGH();
    return response;
}

int8_t ARDUCAMSPIRead(uint8_t addr) {
    CS_LOW();
    
    uint8_t value; // Init val 
    uint8_t rx[] = { 0 };
    
    int32_t response = io_read(arducam_spi_io, rx, sizeof(rx));
    if(response != (int32_t) sizeof(rx)) {
        return -1;
    }
    
    // Value is stored in rx buffer after read
    value = rx[0];

    CS_HIGH();
    return value;
}
