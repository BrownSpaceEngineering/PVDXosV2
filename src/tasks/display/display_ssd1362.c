#include "display_ssd1362.h"

// RST functions
#define RST_LOW() gpio_set_pin_level(OLED_RST_PIN, 0)
#define RST_HIGH() gpio_set_pin_level(OLED_RST_PIN, 1)
// DC functions
#define DC_LOW() gpio_set_pin_level(OLED_DC_PIN, 0)
#define DC_HIGH() gpio_set_pin_level(OLED_DC_PIN, 1)
// CS functions
#define CS_LOW() gpio_set_pin_level(OLED_CS_PIN, 0)
#define CS_HIGH() gpio_set_pin_level(OLED_CS_PIN, 1)
// Reset functions
#define RST_LOW() gpio_set_pin_level(OLED_RST_PIN, 0)
#define CS_HIGH() gpio_set_pin_level(OLED_RST_PIN, 1)

#define INIT_WAIT_INTERVAL 300
#define RESET_HIGH() gpio

status_t spi_write_byte(uint8_t byte) {
    unsigned char res;
    struct spi_xfer xfer;
    xfer.size = 1;
    xfer.txbuf = &byte;
    xfer.rxbuf = &res;
    spi_m_sync_enable(&SPI_0);  // if you forget this line, this function returns -20
    int32_t response = spi_m_sync_transfer(&SPI_0, &xfer);
    if (response != 0) {
        return ERROR_IO;
    }
    return SUCCESS;
}

// TODO: Add error checking
status_t init_display() {
    // Reset the display
    RST_LOW();
    vTaskDelay(pdMS_TO_TICKS(INIT_WAIT_INTERVAL));
    RST_HIGH();
    vTaskDelay(pdMS_TO_TICKS(INIT_WAIT_INTERVAL));

    // Keep CS low for init
    CS_LOW();
    // Unlock command lock (just in case)
    spi_write_byte(SSD1362_CMD_2B_COMMANDLOCK);
    spi_write_byte(SSD_1362_ARG_COMMANDLOCK_UNLOCK);
    // Put display to sleep
    spi_write_byte(SSD1362_CMD_1B_DISPLAYOFF);

    // Configure row and col addrs
    spi_write_byte(SSD1362_CMD_3B_SETCOLUMN);
    spi_write_byte(SSD_1362_COL_START);
    spi_write_byte(SSD_1362_COL_END);
    spi_write_byte(SSD1362_CMD_3B_SETROW);
    spi_write_byte(SSD_1362_ROW_START);
    spi_write_byte(SSD_1362_ROW_END);

    // Set contrast
    spi_write_byte(SSD1362_CMD_2B_CONTRASTMASTER);
    spi_write_byte(SSD1362_CONTRAST_STEP);

    // Set remap
    spi_write_byte(SSD1362_CMD_2B_SETREMAP);
    spi_write_byte(SSD1362_REMAP_VALUE);

    // Set display start line
    spi_write_byte(SSD1362_CMD_2B_STARTLINE);
    spi_write_byte(0x00);

    // Set normal display mode
    spi_write_byte(SSD1362_CMD_1B_NORMALDISPLAY);

    // Set multiplex ratio
    spi_write_byte(SSD1362_CMD_2B_MULTIPLEX_RATIO);
    spi_write_byte(SSD1362_MUX_RATIO);

    // Set VDD
    spi_write_byte(SSD1362_CMD_2B_SET_VDD);
    spi_write_byte(SSD_1362_ARG_VDD_ON);

    // Set IREF
    spi_write_byte(SSD1362_CMD_2B_IREF_SELECTION);
    spi_write_byte(SSD_1362_ARG_IREF_INTERNAL);

    // Set phase length
    spi_write_byte(SSD1362_CMD_2B_PHASE_LENGTH);
    spi_write_byte(SSD_1362_PHASE_1_2_LENGTHS);

    // Set display clock divider
    spi_write_byte(SSD1362_CMD_2B_CLOCKDIV);
    spi_write_byte(SSD1362_CLOCK_DIVIDER_VALUE);

    // Set pre-charge 2 period
    spi_write_byte(SSD1362_CMD_2B_PRECHARGE2);
    spi_write_byte(SSD1362_PRECHARGE_2_TIME);

    // Set linear LUT
    spi_write_byte(SSD1362_CMD_1B_USELINEARLUT);

    // Set pre-charge voltage level to 0.5 * Vcc
    spi_write_byte(SSD1362_CMD_2B_PRECHARGELEVEL);
    spi_write_byte(SSD1362_PRECHARGE_VOLTAGE_RATIO);

    // Set pre-charge capacitor
    spi_write_byte(SSD1362_CMD_2B_PRECHARGE_CAPACITOR);
    spi_write_byte(SSD1362_PRECHARGE_CAPACITOR);

    // Set COM deselect voltage
    spi_write_byte(SSD1362_CMD_2B_COM_DESELECT_VOLTAGE);
    spi_write_byte(SSD1362_DESELECT_VOLTAGE_RATIO);

    // Turn the display on!
    spi_write_byte(SSD1362_CMD_1B_DISPLAYON);

    // CS high when we finish our SPI operations
    CS_HIGH();

    return SUCCESS;
}

status_t write_command(uint8_t cmd) {
    CS_LOW();
    DC_LOW();
    // Spi_write(cmd);
    CS_HIGH();

    return SUCCESS;
}

status_t write_data(uint8_t data) {
    CS_LOW();
    DC_HIGH();
    // Spi_write(cmd);
    CS_HIGH();

    return SUCCESS;
}