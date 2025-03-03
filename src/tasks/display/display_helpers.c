/**
 * display_helpers.c
 *
 * Driver for the SSD1362 OLED controller within a Midas Displays MDOB256064D1Y-YS display.
 *
 * Created: February 29, 2024
 * Authors: Tanish Makadia, Ignacio Blancas Rodriguez, Aidan Wang, Siddharta Laloux
 */

#include "display_task.h"

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
color_t display_buffer[(SSD1362_WIDTH / 2) * SSD1362_HEIGHT] = {0x00};

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

// Overwrites the display buffer with the inputted bytes and triggers an update of the display
status_t display_image(const color_t *const p_buffer) {
    debug("display: Displaying new image\n");
    status_t status;

    display_set_buffer(p_buffer);
    if ((status = display_update()) != SUCCESS)
        return status;

    return SUCCESS;
}

// Clears the display buffer and triggers an update of the display
status_t clear_image(void) {
    debug("display: Clearing currently displayed image\n");
    status_t status;

    display_clear_buffer();
    if ((status = display_update()) != SUCCESS)
        return status;

    return SUCCESS;
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

// Write the contents of spi_tx_buffer to the display
status_t spi_transfer(void) {
    DC_LOW(); // set D/C# pin low to indicate that sent bytes are commands (not data)
    CS_LOW(); // select the display for SPI communication

    int32_t response = spi_m_sync_transfer(&SPI_0, &xfer);
    if (response != (int32_t)xfer.size) {
        return ERROR_IO;
    }

    CS_HIGH(); // deselect the display for SPI communication
    return SUCCESS;
}

// Set the display window to cover the entire screen
status_t display_set_window() {
    status_t status;

    xfer.size = 3;
    spi_tx_buffer[0] = SSD1362_CMD_3B_SETCOLUMN;
    spi_tx_buffer[1] = SSD_1362_COL_START;
    spi_tx_buffer[2] = SSD_1362_COL_END;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    xfer.size = 3;
    spi_tx_buffer[0] = SSD1362_CMD_3B_SETROW;
    spi_tx_buffer[1] = SSD_1362_ROW_START;
    spi_tx_buffer[2] = SSD_1362_ROW_END;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    return SUCCESS;
}

// Set a specific pixel in the display buffer to a given color_t. To actually update the display, call display_update()
status_t display_set_buffer_pixel(point_t x, point_t y, color_t color) {
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
void display_set_buffer(const color_t *const p_buffer) {
    for (uint16_t i = 0; i < (SSD1362_WIDTH / 2) * SSD1362_HEIGHT; i++) {
        display_buffer[i] = p_buffer[i];
    }
}

// Clear the display buffer. To actually update the display, call display_update()
void display_clear_buffer(void) {
    for (uint16_t i = 0; i < (SSD1362_WIDTH / 2) * SSD1362_HEIGHT; i++) {
        display_buffer[i] = 0x00;
    }
}

// Update the display with the contents of the display buffer
status_t display_update(void) {
    status_t status;

    // set the display window to the entire display
    if ((status = display_set_window()) != SUCCESS)
        return status;

    // write the display buffer to the display
    xfer.size = (SSD1362_WIDTH / 2) * SSD1362_HEIGHT;

    for (uint16_t i = 0; i < xfer.size; i++) {
        spi_tx_buffer[i] = display_buffer[i];
    }

    if ((status = spi_transfer()) != SUCCESS)
        return status;

    return SUCCESS;
}

// Initialize the display
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

    status_t status;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Put display to sleep
    xfer.size = 1;
    spi_tx_buffer[0] = SSD1362_CMD_1B_DISPLAYOFF;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Set active display window to the entire display
    if ((status = display_set_window() != SUCCESS))
        return status;

    // Set contrast
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_CONTRASTMASTER;
    spi_tx_buffer[1] = SSD1362_CONTRAST_STEP;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Set remap
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_SETREMAP;
    spi_tx_buffer[1] = SSD1362_REMAP_VALUE;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Set display start line
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_STARTLINE;
    spi_tx_buffer[1] = 0x00;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Set display offset
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_DISPLAYOFFSET;
    spi_tx_buffer[1] = 0x00;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Set display mode
    xfer.size = 1;
    spi_tx_buffer[0] = SSD1362_CMD_1B_NORMALDISPLAY;
    // spi_tx_buffer[0] = SSD1362_CMD_ALLPIXELON; // sets all pixels to max brightness (use for debugging)
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Set multiplex ratio
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_MULTIPLEX_RATIO;
    spi_tx_buffer[1] = SSD1362_MUX_RATIO;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Set VDD
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_SET_VDD;
    spi_tx_buffer[1] = SSD_1362_ARG_VDD_ON;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Set IREF
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_IREF_SELECTION;
    spi_tx_buffer[1] = SSD_1362_ARG_IREF_INTERNAL;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Set phase length
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_PHASE_LENGTH;
    spi_tx_buffer[1] = SSD_1362_PHASE_1_2_LENGTHS;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Set display clock divider
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_CLOCKDIV;
    spi_tx_buffer[1] = SSD1362_CLOCK_DIVIDER_VALUE;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Set pre-charge 2 period
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_PRECHARGE2;
    spi_tx_buffer[1] = SSD1362_PRECHARGE_2_TIME;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Set linear LUT
    xfer.size = 1;
    spi_tx_buffer[0] = SSD1362_CMD_1B_USELINEARLUT;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Set pre-charge voltage level to 0.5 * Vcc
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_PRECHARGELEVEL;
    spi_tx_buffer[1] = SSD1362_PRECHARGE_VOLTAGE_RATIO;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Set pre-charge capacitor
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_PRECHARGE_CAPACITOR;
    spi_tx_buffer[1] = SSD1362_PRECHARGE_CAPACITOR;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Set COM deselect voltage
    xfer.size = 2;
    spi_tx_buffer[0] = SSD1362_CMD_2B_COM_DESELECT_VOLTAGE;
    spi_tx_buffer[1] = SSD1362_DESELECT_VOLTAGE_RATIO;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Turn the display on!
    xfer.size = 1;
    spi_tx_buffer[0] = SSD1362_CMD_1B_DISPLAYON;
    if ((status = spi_transfer()) != SUCCESS)
        return status;

    // Clear the display buffer
    if ((status = clear_image()) != SUCCESS)
        return status;
    return SUCCESS;
}

command_t get_display_image_command(const color_t *const p_buffer) {
    // NOTE: Be sure to use a pointer to a static lifetime variable to ensure
    // that `*p_data` is still valid when the command is received.
    command_t cmd = {p_display_task, OPERATION_DISPLAY_IMAGE, p_buffer, sizeof(color_t *), PROCESSING, NULL};
    return cmd;
}

void exec_command_display(command_t *const p_cmd) {
    if (p_cmd->target != p_display_task) {
        fatal("display: command target is not display! target: %s operation: %d\n", p_cmd->target->name, p_cmd->operation);
    }

    switch (p_cmd->operation) {
        case OPERATION_DISPLAY_IMAGE:
            p_cmd->result = display_image((const color_t *)p_cmd->p_data);
            break;
        case OPERATION_CLEAR_IMAGE:
            p_cmd->result = clear_image();
            break;
        default:
            fatal("display: Invalid operation! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
            break;
    }
}

QueueHandle_t init_display(void) {
    // Initialize the display hardware
    status_t status = init_display_hardware();

    if (status != SUCCESS) {
        fatal("Failed to initialize display hardware!\n");
    }

    // Initialize the display command queue
    QueueHandle_t display_command_queue_handle = xQueueCreateStatic(
        COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, display_mem.display_command_queue_buffer, &display_mem.display_task_queue);
    if (display_command_queue_handle == NULL) {
        fatal("Failed to create display queue!\n");
    }

    return display_command_queue_handle;
}