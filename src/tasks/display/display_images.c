#include "display_ssd1362.h"

// A collection of predefined 256 x 64 images for the display.

void display_checkerboard() {
    for (POINT y = 0; y < SSD1362_HEIGHT; y++) {
        for (POINT x = 0; x < SSD1362_WIDTH; x++) {
            if ((x + y) % 2 == 0) {
                display_set_color(x, y, 0x0F);
            } else {
                display_set_color(x, y, 0x00);
            }
        }
    }
}