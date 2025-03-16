#include "display_driver.h"

// Buffer for SPI transactions
uint8_t spi_rx_buffer[DISPLAY_SPI_BUFFER_CAPACITY] = {0x00};
uint8_t spi_tx_buffer[DISPLAY_SPI_BUFFER_CAPACITY] = {0x00};
struct spi_xfer xfer = {.rxbuf = spi_rx_buffer, .txbuf = spi_tx_buffer, .size = 0};

// Buffer for the display
color_t display_buffer[(SSD1362_WIDTH / 2) * SSD1362_HEIGHT] = {0x00};

/**
 * \fn spi_transfer
 *
 * \brief Performs an SPI transfer to the display
 *
 * \param data whether the data in the `spi_xfer` data (true) or commands (false)
 *
 * \returns `status_t`, whether the transfer operation was successful
 *
 * \warning returns `ERROR_SPI_TRANSFER_FAILED` if transfer unsuccessful
 */
status_t spi_transfer(bool data) {
    if (data) {
        DC_HIGH();
    } else {
        DC_LOW(); // set D/C# pin low to indicate that sent bytes are commands (not data)
    }
    CS_LOW(); // select the display for SPI communication

    int32_t response = spi_m_sync_transfer(&SPI_0, &xfer);
    if (response != (int32_t)xfer.size) {
        return ERROR_SPI_TRANSFER_FAILED;
    }

    CS_HIGH(); // deselect the display for SPI communication
    return SUCCESS;
}

/**
 * \fn display_set_window()
 *
 * \brief Set the display window to cover the entire screen
 *
 * \returns `status_t`, whether the transfer operation was successful
 */
status_t display_set_window(void) {
    xfer.size = 3;
    spi_tx_buffer[0] = SSD1362_CMD_3B_SETCOLUMN;
    spi_tx_buffer[1] = SSD_1362_COL_START;
    spi_tx_buffer[2] = SSD_1362_COL_END;

    ret_err_status(spi_transfer(false), "display: spi_transfer(false) failed");

    xfer.size = 3;
    spi_tx_buffer[0] = SSD1362_CMD_3B_SETROW;
    spi_tx_buffer[1] = SSD_1362_ROW_START;
    spi_tx_buffer[2] = SSD_1362_ROW_END;

    ret_err_status(spi_transfer(false), "display: spi_transfer(false) failed");

    return SUCCESS;
}

/**
 * \fn display_set_buffer_pixel
 *
 * \brief Set a specific pixel in the display buffer to a given `color_t`. To
 *        actually update the display, call display_update()
 *
 * \param x the x-coordinate of the pixel
 * \param y the y-coordinate of the pixel
 * \param color the color to set the pixel to
 *
 * \returns `status_t`, whether the operation was successful
 */
status_t display_set_buffer_pixel(point_t x, point_t y, color_t color) {
    // bounds checking
    if (x >= SSD1362_WIDTH || y >= SSD1362_HEIGHT) {
        return ERROR_SANITY_CHECK_FAILED;
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

/**
 * \fn display_set_buffer
 *
 * \brief Set the entire display buffer to the contents of the input buffer. To
 *        actually update the display, call display_update()
 *
 * \param p_buffer the display buffer to be set
 *
 */
void display_set_buffer(const color_t *const p_buffer) {
    for (uint16_t i = 0; i < (SSD1362_WIDTH / 2) * SSD1362_HEIGHT; i++) {
        display_buffer[i] = p_buffer[i];
    }
}

/**
 * \fn display_clear_buffer
 *
 * \brief Clear the display buffer. To actually update the display,
 *        call display_update()
 */
void display_clear_buffer(void) {
    for (uint16_t i = 0; i < (SSD1362_WIDTH / 2) * SSD1362_HEIGHT; i++) {
        display_buffer[i] = 0x00;
    }
}

/**
 * \fn display_update
 *
 * \brief Update the display with the contents of the display buffer
 *
 * \returns `status_t`, whether the operation was successful
 */
status_t display_update(void) {
    // set the display window to the entire display
    ret_err_status(display_set_window(), "display: set_window failed");

    // write the display buffer to the display
    xfer.size = (SSD1362_WIDTH / 2) * SSD1362_HEIGHT;

    for (uint16_t i = 0; i < xfer.size; i++) {
        spi_tx_buffer[i] = display_buffer[i];
    }

    ret_err_status(spi_transfer(true), "display: spi_transer(true) failed");

    return SUCCESS;
}

/**
 * \fn init_display_hardware
 *
 * \brief Initializes the display hardware
 *
 * \returns `status_t`, whether the operation was successful
 */
status_t init_display_hardware(void) {
    spi_m_sync_enable(&SPI_0); // if you forget this line, this function returns -20

    // Reset the display by setting RST to low (it should be high during normal operation)
    RST_HIGH();
    vTaskDelay(pdMS_TO_TICKS(RESET_WAIT_INTERVAL));
    RST_LOW();
    vTaskDelay(pdMS_TO_TICKS(RESET_WAIT_INTERVAL));
    RST_HIGH();
    vTaskDelay(pdMS_TO_TICKS(RESET_WAIT_INTERVAL));

    // Unlock command lock (just in case)
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_COMMANDLOCK;
    spi_tx_buffer[1] = SSD_1362_ARG_COMMANDLOCK_UNLOCK;

    fatal_on_error(spi_transfer(false), "display hardware init: could not unlock command lock");

    // Put display to sleep
    xfer.size = 1;
    spi_tx_buffer[0] = SSD1362_CMD_1B_DISPLAYOFF;

    fatal_on_error(spi_transfer(false), "display hardware init: could not put display to sleep");

    // Set active display window to the entire display
    fatal_on_error(display_set_window(), "display hardware init: could not set active window to whole display");

    // Set contrast
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_CONTRASTMASTER;
    spi_tx_buffer[1] = SSD1362_CONTRAST_STEP;

    fatal_on_error(spi_transfer(false), "display hardware init: could not set contrast");

    // Set remap
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_SETREMAP;
    spi_tx_buffer[1] = SSD1362_REMAP_VALUE;

    fatal_on_error(spi_transfer(false), "display hardware init: could not set remap");

    // Set display start line
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_STARTLINE;
    spi_tx_buffer[1] = 0x00;

    fatal_on_error(spi_transfer(false), "display hardware init: could not set display start line");

    // Set display offset
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_DISPLAYOFFSET;
    spi_tx_buffer[1] = 0x00;

    fatal_on_error(spi_transfer(false), "display hardware init: could not set display offset");

    // Set display mode
    xfer.size = 1;
    spi_tx_buffer[0] = SSD1362_CMD_1B_NORMALDISPLAY;
    // spi_tx_buffer[0] = SSD1362_CMD_ALLPIXELON; // sets all pixels to max brightness (use for debugging)
    fatal_on_error(spi_transfer(false), "display hardware init: could not set display mode");

    // Set multiplex ratio
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_MULTIPLEX_RATIO;
    spi_tx_buffer[1] = SSD1362_MUX_RATIO;

    fatal_on_error(spi_transfer(false), "display hardware init: could not set multiplex ratio");

    // Set VDD
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_SET_VDD;
    spi_tx_buffer[1] = SSD_1362_ARG_VDD_ON;

    fatal_on_error(spi_transfer(false), "display hardware init: could not set VDD");

    // Set IREF
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_IREF_SELECTION;
    spi_tx_buffer[1] = SSD_1362_ARG_IREF_INTERNAL;

    fatal_on_error(spi_transfer(false), "display hardware init: could not set IREF");

    // Set phase length
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_PHASE_LENGTH;
    spi_tx_buffer[1] = SSD_1362_PHASE_1_2_LENGTHS;

    fatal_on_error(spi_transfer(false), "display hardware init: could not set phase length");

    // Set display clock divider
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_CLOCKDIV;
    spi_tx_buffer[1] = SSD1362_CLOCK_DIVIDER_VALUE;

    fatal_on_error(spi_transfer(false), "display hardware init: could not set display clock divider");

    // Set pre-charge 2 period
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_PRECHARGE2;
    spi_tx_buffer[1] = SSD1362_PRECHARGE_2_TIME;

    fatal_on_error(spi_transfer(false), "display hardware init: could not set pre-charge 2 periods");

    // Set linear LUT
    xfer.size = 1;
    spi_tx_buffer[0] = SSD1362_CMD_1B_USELINEARLUT;

    fatal_on_error(spi_transfer(false), "display hardware init: could not set linear LUT");

    // Set pre-charge voltage level to 0.5 * Vcc
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_PRECHARGELEVEL;
    spi_tx_buffer[1] = SSD1362_PRECHARGE_VOLTAGE_RATIO;

    fatal_on_error(spi_transfer(false), "display hardware init: could not set pre-charge voltage");

    // Set pre-charge capacitor
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_PRECHARGE_CAPACITOR;
    spi_tx_buffer[1] = SSD1362_PRECHARGE_CAPACITOR;

    fatal_on_error(spi_transfer(false), "display hardware init: could not set pre-charge capacitor");

    // Set COM deselect voltage
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_COM_DESELECT_VOLTAGE;
    spi_tx_buffer[1] = SSD1362_DESELECT_VOLTAGE_RATIO;

    fatal_on_error(spi_transfer(false), "display hardware init: could not set COM deselect voltage");

    // Turn the display on!
    xfer.size = 1;
    spi_tx_buffer[0] = SSD1362_CMD_1B_DISPLAYON;

    fatal_on_error(spi_transfer(false), "display hardware init: could not turn display on");

    // Clear the display buffer
    display_clear_buffer();

    // Trigger an update so the empty buffer is actually displayed
    ret_err_status(display_update(), "display: Update failed");

    return SUCCESS;
}