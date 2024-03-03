#include "display_ssd1362.h"

// Comment for future development

// RST functions
#define RST_LOW() gpio_set_pin_level(OLED_RST_PIN, 0)
#define RST_HIGH() gpio_set_pin_level(OLED_RST_PIN, 1)
// DC functions
#define RST_LOW() gpio_set_pin_level(OLED_DC_PIN, 0)
#define RST_HIGH() gpio_set_pin_level(OLED_DC_PIN, 1)
// CS functions
#define CS_LOW() gpio_set_pin_level(OLED_CS_PIN, 0)
#define CS_HIGH() gpio_set_pin_level(OLED_CS_PIN, 1)