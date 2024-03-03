#ifndef DISPLAY_H
#define DISPLAY_H
// Includes
#include <atmel_start.h>
#include <globals.h>

// Driver for the SSD1362 OLED controller within a Midas Displays MDOB256064D1Y-YS display

// SSD1362 commands
#define SSD1362_WIDTH                 256
#define SSD1362_HEIGHT                64
#define SSD1362_CMD_SETCOLUMN         0x15
#define SSD1362_CMD_SETROW            0x75

#define SSD1362_CMD_WRITERAM          0x5C
#define SSD1362_CMD_READRAM           0x5D

#define SSD1362_CMD_SETREMAP          0xA0
#define SSD1362_CMD_STARTLINE         0xA1
#define SSD1362_CMD_DISPLAYOFFSET     0xA2
#define SSD1362_CMD_VERT_SCROLL_AREA  0xA3
#define SSD1362_CMD_DISPLAYALLOFF     0xA6
#define SSD1362_CMD_DISPLAYALLON      0xA5
#define SSD1362_CMD_NORMALDISPLAY     0xA4
#define SSD1362_CMD_INVERTDISPLAY     0xA7
#define SSD1362_CMD_MULTIPLEX_RATIO   0xA8
#define SSD1362_CMD_FUNCTIONSELECT    0xAB
#define SSD1362_CMD_IREF_SELECTION    0xAD
#define SSD1362_CMD_DISPLAYOFF        0xAE
#define SSD1362_CMD_DISPLAYON         0xAF
#define SSD1362_CMD_PRECHARGE         0xB1
#define SSD1362_CMD_DISPLAYENHANCE    0xB2
#define SSD1362_CMD_CLOCKDIV          0xB3
#define SSD1362_CMD_SETVSL            0xB4
#define SSD1362_CMD_SETGPIO           0xB5
#define SSD1362_CMD_PRECHARGE2        0xB6
#define SSD1362_CMD_SETGRAY           0xB8
#define SSD1362_CMD_USELUT            0xB9
#define SSD1362_CMD_PRECHARGELEVEL    0xBC
#define SSD1362_CMD_VCOMH             0xBE

#define SSD1362_CMD_CONTRASTABC       0xC1
#define SSD1362_CMD_CONTRASTMASTER    0xC7
#define SSD1362_CMD_MUXRATIO          0xCA
#define SSD1362_CMD_COMMANDLOCK       0xFD
#define SSD1362_CMD_HORIZSCROLL       0x96
#define SSD1362_CMD_STOPSCROLL        0x9E
#define SSD1362_CMD_STARTSCROLL       0x9F

// Pin definitions
#define OLED_RST_PIN 25
#define OLED_DC_PIN 24
#define OLED_CS_PIN 8

// Color definitions
#define BLACK    0x0000
#define BLUE     0x001F
#define RED      0xF800
#define GREEN    0x07E0
#define CYAN     0x07FF
#define MAGENTA  0xF81F
#define YELLOW   0xFFE0
#define WHITE    0xFFFF

#endif // DISPLAY_H