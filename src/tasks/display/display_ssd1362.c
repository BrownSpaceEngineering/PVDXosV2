#include "display_ssd1362.h"

// functions for setting the reset, D/C#, and CS# pins on the display to high or low voltage
#define RST_LOW() gpio_set_pin_level(Display_RST, 0)
#define RST_HIGH() gpio_set_pin_level(Display_RST, 1)
#define DC_LOW() gpio_set_pin_level(Display_DC, 0)
#define DC_HIGH() gpio_set_pin_level(Display_DC, 1)
#define CS_LOW() gpio_set_pin_level(Display_CS, 0)
#define CS_HIGH() gpio_set_pin_level(Display_CS, 1)

// duration to wait between display initialization steps
#define RESET_WAIT_INTERVAL 100

// maximum number of bytes that can be sent to the display in a single SPI transaction
#define DISPLAY_SPI_BUFFER_CAPACITY 3

// buffer for SPI transactions
uint8_t spi_rx_buffer[DISPLAY_SPI_BUFFER_CAPACITY] = {0};
uint8_t spi_tx_buffer[DISPLAY_SPI_BUFFER_CAPACITY] = {0};
struct spi_xfer xfer = {
    .rxbuf = spi_rx_buffer,
    .txbuf = spi_tx_buffer,
    .size = 0
};


// write the contents of spi_tx_buffer to the display
status_t spi_write() {
    int32_t response = spi_m_sync_transfer(&SPI_0, &xfer);
    if (response != (int32_t)xfer.size) {
        return ERROR_IO;
    }
    return SUCCESS;
}


// TODO: change to vTaskDelay and add error checking
void display_reset(void) {
    RST_HIGH();
    delay_ms(RESET_WAIT_INTERVAL);
    RST_LOW();
    delay_ms(RESET_WAIT_INTERVAL);
    RST_HIGH();
    delay_ms(RESET_WAIT_INTERVAL);
}


// TODO: Add error checking and determine IREF
status_t display_init() {
    spi_m_sync_enable(&SPI_0); // if you forget this line, this function returns -20

    display_reset(); // setting reset pin low triggers a reset of the display

    CS_LOW(); // select the display for SPI communication
    DC_LOW(); // set D/C# pin low to indicate that sent bytes are commands (not data)

    // Unlock command lock (just in case)
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_COMMANDLOCK;
    spi_tx_buffer[1] = SSD_1362_ARG_COMMANDLOCK_UNLOCK;
    spi_write();

    // Put display to sleep
    xfer.size = 1;
    spi_tx_buffer[0] = SSD1362_CMD_1B_DISPLAYOFF;
    spi_write();

    // Configure row and col addrs
    xfer.size = 3;
    spi_tx_buffer[0] = SSD1362_CMD_3B_SETCOLUMN;
    spi_tx_buffer[1] = SSD_1362_COL_START;
    spi_tx_buffer[2] = SSD_1362_COL_END;
    spi_write();

    xfer.size = 3;
    spi_tx_buffer[0] = SSD1362_CMD_3B_SETROW;
    spi_tx_buffer[1] = SSD_1362_ROW_START;
    spi_tx_buffer[2] = SSD_1362_ROW_END;
    spi_write();

    // Set contrast
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_CONTRASTMASTER;
    spi_tx_buffer[1] = SSD1362_CONTRAST_STEP;
    spi_write();

    // Set remap
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_SETREMAP;
    spi_tx_buffer[1] = SSD1362_REMAP_VALUE;
    spi_write();

    // Set display start line
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_STARTLINE;
    spi_tx_buffer[1] = 0x00;
    spi_write();

    // Set display offset
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_DISPLAYOFFSET;
    spi_tx_buffer[1] = 0x00;
    spi_write();

    // Set normal display mode
    xfer.size = 1;
    spi_tx_buffer[0] = SSD1362_CMD_ALLPIXELON; // was 0xA4 for normal display previously
    spi_write();

    // Set multiplex ratio
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_MULTIPLEX_RATIO;
    spi_tx_buffer[1] = SSD1362_MUX_RATIO;
    spi_write();

    // Set VDD
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_SET_VDD;
    spi_tx_buffer[1] = SSD_1362_ARG_VDD_ON;
    spi_write();

    // Set IREF
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_IREF_SELECTION;
    spi_tx_buffer[1] = SSD_1362_ARG_IREF_EXTERNAL; // possibly should be 0x9E for internal
    spi_write();

    // Set phase length
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_PHASE_LENGTH;
    spi_tx_buffer[1] = SSD_1362_PHASE_1_2_LENGTHS;
    spi_write();

    // Set display clock divider
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_CLOCKDIV;
    spi_tx_buffer[1] = SSD1362_CLOCK_DIVIDER_VALUE;
    spi_write();

    // Set pre-charge 2 period
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_PRECHARGE2;
    spi_tx_buffer[1] = SSD1362_PRECHARGE_2_TIME;
    spi_write();

    // Set linear LUT
    xfer.size = 1;
    spi_tx_buffer[0] = SSD1362_CMD_1B_USELINEARLUT;
    spi_write();

    // Set pre-charge voltage level to 0.5 * Vcc
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_PRECHARGELEVEL;
    spi_tx_buffer[1] = SSD1362_PRECHARGE_VOLTAGE_RATIO;
    spi_write();

    // Set pre-charge capacitor
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_PRECHARGE_CAPACITOR;
    spi_tx_buffer[1] = SSD1362_PRECHARGE_CAPACITOR;
    spi_write();

    // Set COM deselect voltage
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_COM_DESELECT_VOLTAGE;
    spi_tx_buffer[1] = SSD1362_DESELECT_VOLTAGE_RATIO;
    spi_write();

    // Turn the display on!
    xfer.size = 1;
    spi_tx_buffer[0] = SSD1362_CMD_1B_DISPLAYON;
    spi_write();

    CS_HIGH(); // deselect the display for SPI communication

    return SUCCESS;
}