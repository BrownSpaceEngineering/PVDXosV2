#include "display_ssd1362.h"

// Comment for future development

// RST functions
#define RST_LOW() gpio_set_pin_level(OLED_RST_PIN, 0)
#define RST_HIGH() gpio_set_pin_level(OLED_RST_PIN, 1)
// DC functions
#define DC_LOW() gpio_set_pin_level(OLED_DC_PIN, 0)
#define DC_HIGH() gpio_set_pin_level(OLED_DC_PIN, 1)
// CS functions
#define CS_LOW() gpio_set_pin_level(OLED_CS_PIN, 0)
#define CS_HIGH() gpio_set_pin_level(OLED_CS_PIN, 1)

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