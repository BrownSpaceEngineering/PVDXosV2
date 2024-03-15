#ifndef DISPLAY_H
#define DISPLAY_H
// Includes
#include <atmel_start.h>
#include <globals.h>

// Driver for the SSD1362 OLED controller within a Midas Displays MDOB256064D1Y-YS display.

// SSD1362 commands
#define SSD1362_WIDTH                       256
#define SSD1362_HEIGHT                      64
#define SSD1362_CMD_3B_SETCOLUMN            0x15
#define SSD1362_CMD_3B_SETROW               0x75

#define SSD1362_CMD_WRITERAM                0x5C
#define SSD1362_CMD_READRAM                 0x5D

#define SSD1362_CMD_2B_SETREMAP             0xA0
#define SSD1362_CMD_2B_STARTLINE            0xA1
#define SSD1362_CMD_DISPLAYOFFSET           0xA2
#define SSD1362_CMD_VERT_SCROLL_AREA        0xA3
#define SSD1362_CMD_DISPLAYALLOFF           0xA6
#define SSD1362_CMD_DISPLAYALLON            0xA5
#define SSD1362_CMD_1B_NORMALDISPLAY        0xA4
#define SSD1362_CMD_ALLPIXELON              0xA5
#define SSD1362_CMD_INVERTDISPLAY           0xA7
#define SSD1362_CMD_2B_MULTIPLEX_RATIO      0xA8
#define SSD1362_CMD_2B_SET_VDD              0xAB
#define SSD1362_CMD_2B_IREF_SELECTION       0xAD
#define SSD1362_CMD_1B_DISPLAYOFF           0xAE
#define SSD1362_CMD_1B_DISPLAYON            0xAF
#define SSD1362_CMD_2B_PHASE_LENGTH         0xB1
#define SSD1362_CMD_DISPLAYENHANCE          0xB2
#define SSD1362_CMD_2B_CLOCKDIV             0xB3
#define SSD1362_CMD_SETVSL                  0xB4
#define SSD1362_CMD_SETGPIO                 0xB5
#define SSD1362_CMD_2B_PRECHARGE2           0xB6
#define SSD1362_CMD_SETGRAY                 0xB8
#define SSD1362_CMD_1B_USELINEARLUT         0xB9
#define SSD1362_CMD_2B_PRECHARGELEVEL       0xBC
#define SSD1362_CMD_2B_PRECHARGE_CAPACITOR  0xBD
#define SSD1362_CMD_2B_COM_DESELECT_VOLTAGE 0xBE

#define SSD1362_CMD_2B_CONTRASTMASTER       0x81
#define SSD1362_CMD_2B_COMMANDLOCK          0xFD
#define SSD1362_CMD_HORIZSCROLL             0x96
#define SSD1362_CMD_STOPSCROLL              0x9E
#define SSD1362_CMD_STARTSCROLL             0x9F

#define SSD_1362_ARG_COMMANDLOCK_UNLOCK     0x12
#define SSD_1362_ARG_VDD_ON                 0x01
#define SSD_1362_ARG_VDD_OFF                0x00
#define SSD_1362_ARG_IREF_EXTERNAL          0x8E
#define SSD_1362_ARG_IREF_INTERNAL          0x9E

// Configuration for our row/col configuration
#define SSD_1362_ROW_START                  0x00
#define SSD_1362_ROW_END                    0x3F
#define SSD_1362_COL_START                  0x00
#define SSD_1362_COL_END                    0x7F
#define SSD1362_CONTRAST_STEP               0x2F // TODO: what is this? (used to be 0x25)
#define SSD1362_REMAP_VALUE                 0xC3 // TODO: what is this?
#define SSD1362_MUX_RATIO                   0x3F // TODO: what is this?
#define SSD_1362_PHASE_1_2_LENGTHS          0x22 // TODO: what is this?
#define SSD1362_CLOCK_DIVIDER_VALUE         0xA0 // TODO: what is this?
#define SSD1362_PRECHARGE_2_TIME            0x04 // TODO: what is this?
#define SSD1362_PRECHARGE_VOLTAGE_RATIO     0x10 // TODO: Rust/Python use 0x04
#define SSD1362_PRECHARGE_CAPACITOR         0x01 // TODO: what is this
#define SSD1362_DESELECT_VOLTAGE_RATIO      0x07 // TODO: what is this

// Data types
#define POINT uint16_t // 16 bits per coordinate (larger than 8-bit for overflow checking)
#define COLOR uint8_t // 4 bits per pixel (16 greyscale levels)

// Functions
status_t display_init(void);
status_t display_set_buffer(POINT x, POINT y, COLOR color);

// Predefined images
void display_checkerboard(void);

// Variables
extern COLOR display_buffer[(SSD1362_WIDTH / 2) * SSD1362_HEIGHT]; // pixels are 4 bits, so 2 consecutive pixels per byte

#endif // DISPLAY_H