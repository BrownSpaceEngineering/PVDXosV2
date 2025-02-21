#include <stdint.h>
#include "arducam.h"

// Functions for setting the reset, data/command, and chip-select pins on the display to high or low voltage
#define RST_LOW() gpio_set_pin_level(Display_RST, 0)
#define RST_HIGH() gpio_set_pin_level(Display_RST, 1)
#define DC_LOW() gpio_set_pin_level(Display_DC, 0)
#define DC_HIGH() gpio_set_pin_level(Display_DC, 1)
#define CS_LOW() gpio_set_pin_level(Display_CS, 0)
#define CS_HIGH() gpio_set_pin_level(Display_CS, 1)

// Duration to wait between display initialization steps
#define RESET_WAIT_INTERVAL 100
// Maximum number of bytes that can be sent to the display in a single SPI transaction
#define DISPLAY_SPI_BUFFER_CAPACITY (SSD1362_WIDTH / 2) * SSD1362_HEIGHT

// Buffer for SPI transactions
uint8_t spi_rx_buffer[ARDUCAM_SPI_BUFFER_CAPACITY] = {0x00};
uint8_t spi_tx_buffer[ARDUCAM_SPI_BUFFER_CAPACITY] = {0x00};
struct spi_xfer xfer = {.rxbuf = spi_rx_buffer, .txbuf = spi_tx_buffer, .size = 0};

status_t spi_write_command(void) {
    CS_LOW(); 

    int32_t response = spi_m_sync_transfer(&SPI_0, &xfer);
    if (response != (int32_t)xfer.size) {
        return ERROR_IO;
    }

    CS_HIGH(); // deselect arducam for SPI communication
    return SUCCESS;
}

void ARDUCAMwReg(uint8_t addr, uint8_t data) {
    CS_LOW();

    xfer.size = 1;
    spi_tx_buffer[0] = data;

    int32_t response = spi_m_sync_transfer(&SPI_0, &xfer);
    if(response != (int32_t)xfer.size) {
        return ERROR_IO;
    }

    CS_HIGH();
    return SUCCESS;
}

uint8_t ARDUCAMrReg(uint8_t addr) {
    CS_LOW();
    
    uint8_t value; // Init val 
    xfer.size = 1;
    spi_tx_buffer[0] = addr | 0x80; // 0x80 sends read bit to camera
    
    int32_t response = spi_m_sync_transfer(&SPI_0, &xfer);
    if(response != (int32_t)xfer.size) {
        return ERROR_IO;
    }
    
    // Value is stored in rx buffer after read
    value = spi_rx_buffer[0];

    CS_HIGH();
    return value;

}
