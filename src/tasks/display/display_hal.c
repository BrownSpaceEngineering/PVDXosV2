#include "display_hal.h"

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
uint8_t spi_rx_buffer[DISPLAY_SPI_BUFFER_CAPACITY] = {0x00};
uint8_t spi_tx_buffer[DISPLAY_SPI_BUFFER_CAPACITY] = {0x00};
struct spi_xfer xfer = {.rxbuf = spi_rx_buffer, .txbuf = spi_tx_buffer, .size = 0};

// Buffer for the display
COLOR display_buffer[(SSD1362_WIDTH / 2) * SSD1362_HEIGHT] = {0x00};

// Write the contents of spi_tx_buffer to the display as a command
status_t spi_write_command() {
    DC_LOW(); // set D/C# pin low to indicate that sent bytes are commands (not data)
    CS_LOW(); // select the display for SPI communication

    int32_t response = spi_m_sync_transfer(&SPI_0, &xfer);
    if (response != (int32_t)xfer.size) {
        return ERROR_IO;
    }

    CS_HIGH(); // deselect the display for SPI communication
    return SUCCESS;
}

// Write the contents of spi_tx_buffer to the display as data
status_t spi_write_data() {
    DC_HIGH(); // set D/C# pin high to indicate that sent bytes are data (not commands)
    CS_LOW();  // select the display for SPI communication

    TickType_t start = xTaskGetTickCount();
    int32_t response = spi_m_sync_transfer(&SPI_0, &xfer);
    if (response != (int32_t)xfer.size) {
        return ERROR_IO;
    }
    TickType_t end = xTaskGetTickCount();
    TickType_t duration = end - start;
    int duration_ms = duration * portTICK_RATE_MS;
    debug("display: Duration to send data to display buffer: %d ms\n", duration_ms);

    CS_HIGH(); // deselect the display for SPI communication
    return SUCCESS;
}

// Trigger a complete reset of the display
void display_reset(void) {
    RST_HIGH();
    vTaskDelay(pdMS_TO_TICKS(RESET_WAIT_INTERVAL));
    RST_LOW();
    vTaskDelay(pdMS_TO_TICKS(RESET_WAIT_INTERVAL));
    RST_HIGH();
    vTaskDelay(pdMS_TO_TICKS(RESET_WAIT_INTERVAL));
}

// Set the display window to cover the entire screen
status_t display_set_window() {
    xfer.size = 3;
    spi_tx_buffer[0] = SSD1362_CMD_3B_SETCOLUMN;
    spi_tx_buffer[1] = SSD_1362_COL_START;
    spi_tx_buffer[2] = SSD_1362_COL_END;
    spi_write_command();

    xfer.size = 3;
    spi_tx_buffer[0] = SSD1362_CMD_3B_SETROW;
    spi_tx_buffer[1] = SSD_1362_ROW_START;
    spi_tx_buffer[2] = SSD_1362_ROW_END;
    spi_write_command();

    return SUCCESS;
}

// Set a specific pixel in the display buffer to a given color. To actually update the display, call display_update()
status_t display_set_buffer_pixel(POINT x, POINT y, COLOR color) {
    // bounds checking
    if (x >= SSD1362_WIDTH || y >= SSD1362_HEIGHT) {
        return ERROR_INTERNAL;
    }

    // update the display buffer
    uint16_t index = (y * (SSD1362_WIDTH / 2)) + (x / 2);

    if (x % 2 == 0) {
        display_buffer[index] |= (color << 4); // set the upper 4 bits of the byte
    } else {
        display_buffer[index] |= (color & 0x0F); // set the lower 4 bits of the byte
    }

    return SUCCESS;
}

// Set the entire display buffer to the contents of the input buffer. To actually update the display, call display_update()
status_t display_set_buffer(const COLOR* p_buffer) {
    for (uint16_t i = 0; i < (SSD1362_WIDTH / 2) * SSD1362_HEIGHT; i++) {
        display_buffer[i] = p_buffer[i];
    }

    return SUCCESS;
}

// Clear the display buffer. To actually update the display, call display_update()
status_t display_clear_buffer() {
    for (uint16_t i = 0; i < (SSD1362_WIDTH / 2) * SSD1362_HEIGHT; i++) {
        display_buffer[i] = 0x00;
    }

    return SUCCESS;
}

// Update the display with the contents of the display buffer
status_t display_update() {
    // set the display window to the entire display
    display_set_window();

    // write the display buffer to the display
    xfer.size = (SSD1362_WIDTH / 2) * SSD1362_HEIGHT;

    for (uint16_t i = 0; i < xfer.size; i++) {
        spi_tx_buffer[i] = display_buffer[i];
    }

    spi_write_data();
    return SUCCESS;
}

// Initialize the display
status_t display_init() {
    spi_m_sync_enable(&SPI_0); // if you forget this line, this function returns -20

    display_reset(); // setting reset pin low triggers a reset of the display

    // Unlock command lock (just in case)
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_COMMANDLOCK;
    spi_tx_buffer[1] = SSD_1362_ARG_COMMANDLOCK_UNLOCK;
    spi_write_command();

    // Put display to sleep
    xfer.size = 1;
    spi_tx_buffer[0] = SSD1362_CMD_1B_DISPLAYOFF;
    spi_write_command();

    // Set active display window to the entire display
    display_set_window();

    // Set contrast
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_CONTRASTMASTER;
    spi_tx_buffer[1] = SSD1362_CONTRAST_STEP;
    spi_write_command();

    // Set remap
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_SETREMAP;
    spi_tx_buffer[1] = SSD1362_REMAP_VALUE;
    spi_write_command();

    // Set display start line
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_STARTLINE;
    spi_tx_buffer[1] = 0x00;
    spi_write_command();

    // Set display offset
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_DISPLAYOFFSET;
    spi_tx_buffer[1] = 0x00;
    spi_write_command();

    // Set display mode
    xfer.size = 1;
    spi_tx_buffer[0] = SSD1362_CMD_1B_NORMALDISPLAY;
    // spi_tx_buffer[0] = SSD1362_CMD_ALLPIXELON; // sets all pixels to max brightness (use for debugging)
    spi_write_command();

    // Set multiplex ratio
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_MULTIPLEX_RATIO;
    spi_tx_buffer[1] = SSD1362_MUX_RATIO;
    spi_write_command();

    // Set VDD
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_SET_VDD;
    spi_tx_buffer[1] = SSD_1362_ARG_VDD_ON;
    spi_write_command();

    // Set IREF
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_IREF_SELECTION;
    spi_tx_buffer[1] = SSD_1362_ARG_IREF_INTERNAL;
    spi_write_command();

    // Set phase length
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_PHASE_LENGTH;
    spi_tx_buffer[1] = SSD_1362_PHASE_1_2_LENGTHS;
    spi_write_command();

    // Set display clock divider
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_CLOCKDIV;
    spi_tx_buffer[1] = SSD1362_CLOCK_DIVIDER_VALUE;
    spi_write_command();

    // Set pre-charge 2 period
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_PRECHARGE2;
    spi_tx_buffer[1] = SSD1362_PRECHARGE_2_TIME;
    spi_write_command();

    // Set linear LUT
    xfer.size = 1;
    spi_tx_buffer[0] = SSD1362_CMD_1B_USELINEARLUT;
    spi_write_command();

    // Set pre-charge voltage level to 0.5 * Vcc
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_PRECHARGELEVEL;
    spi_tx_buffer[1] = SSD1362_PRECHARGE_VOLTAGE_RATIO;
    spi_write_command();

    // Set pre-charge capacitor
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_PRECHARGE_CAPACITOR;
    spi_tx_buffer[1] = SSD1362_PRECHARGE_CAPACITOR;
    spi_write_command();

    // Set COM deselect voltage
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_COM_DESELECT_VOLTAGE;
    spi_tx_buffer[1] = SSD1362_DESELECT_VOLTAGE_RATIO;
    spi_write_command();

    // Turn the display on!
    xfer.size = 1;
    spi_tx_buffer[0] = SSD1362_CMD_1B_DISPLAYON;
    spi_write_command();

    // Display an initial image
    display_set_buffer(IMAGE_BUFFER_PVDX);
    display_update();

    return SUCCESS;
}
