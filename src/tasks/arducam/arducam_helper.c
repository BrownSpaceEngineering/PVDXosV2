#include <stdint.h>
#include "arducam.h"

struct spi_xfer ardu_xfer = {.rxbuf = ardu_spi_rx_buffer, .txbuf = ardu_spi_tx_buffer, .size = 0};

int32_t ARDUCAMwReg(uint8_t addr, uint8_t data) {
    CS_LOW();

    ardu_xfer.size = 1;
    ardu_spi_tx_buffer[0] = data;

    int32_t response = spi_m_sync_transfer(&SPI_0, &ardu_xfer);
    if(response != (int32_t)ardu_xfer.size) {
        return -1;
    }

    CS_HIGH();
    return response;
}

int8_t ARDUCAMrReg(uint8_t addr) {
    CS_LOW();
    
    uint8_t value; // Init val 
    ardu_xfer.size = 1;
    ardu_spi_tx_buffer[0] = addr | 0x80; // 0x80 sends read bit to camera
    
    int32_t response = spi_m_sync_transfer(&SPI_0, &ardu_xfer);
    if(response != (int32_t)ardu_xfer.size) {
        return -1;
    }
    
    // Value is stored in rx buffer after read
    value = ardu_spi_rx_buffer[0];

    CS_HIGH();
    return value;

}
