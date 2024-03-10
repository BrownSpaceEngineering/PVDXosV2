#include "uhf_hal.h"

#include "pincfg.h"

#define UHF_BUFFER_CAPACITY 8

uint8_t uhf_tx_buffer[UHF_BUFFER_CAPACITY] = {0};
uint8_t uhf_rx_buffer[UHF_BUFFER_CAPACITY] = {0};
struct spi_xfer uhf_transfer_struct = {
    .rxbuf = uhf_rx_buffer,
    .txbuf = uhf_tx_buffer,
    .size = 0,
};

status_t readRegister(uint8_t address, uint8_t *data) {
    gpio_set_pin_level(UHF_CS, false);      // Set CS low
    uint8_t register_addr = address & 0x7F; // All bits except the first one
    // Clear the buffer just in case
    uhf_tx_buffer[0] = register_addr; // First byte is the register address
    uhf_tx_buffer[1] = 0x00;          // Second byte is the value to write (which is nothing, we are reading)

    uhf_rx_buffer[0] = 0x00; // Clear the buffer so that we read in fresh data
    uhf_rx_buffer[1] = 0x00; // The value from the register will be stored here after the sync_transfer

    uhf_transfer_struct.size = 2; // Capacity is larger than this, but we only want to transfer 2 bytes anyways
    uint32_t bytes_transferred = spi_m_sync_transfer(&SPI_0, &uhf_transfer_struct);
    gpio_set_pin_level(UHF_CS, true); // Set CS high
    if (bytes_transferred != 2) {
        warning("Failed to read from UHF module\n");
        return ERROR_INTERNAL;
    }
    *data = uhf_rx_buffer[1]; // Return the data we read
    vTaskDelay(
        pdMS_TO_TICKS(1)); // Delay 1ms to let things catch up, SPI operations are high-priority and may block other tasks from executing
    return SUCCESS;
}

status_t writeRegister(uint8_t address, uint8_t value) {
    gpio_set_pin_level(UHF_CS, false);      // Set CS low
    uint8_t register_addr = address & 0x7F; // All bits except the first one
    register_addr |= 0x80;                  // Set the first bit to 1 to indicate a write operation
    // Clear the buffer just in case
    uhf_tx_buffer[0] = register_addr; // First byte is the register address
    uhf_tx_buffer[1] = value;         // Second byte is the value to write

    uhf_rx_buffer[0] = 0x00; // Clear the buffer
    uhf_rx_buffer[1] = 0x00;

    uhf_transfer_struct.size = 2; // Capacity is larger than this, but we only want to transfer 2 bytes anyways
    uint32_t bytes_transferred = spi_m_sync_transfer(&SPI_0, &uhf_transfer_struct);
    gpio_set_pin_level(UHF_CS, true); // Set CS high
    if (bytes_transferred != 2) {
        warning("Failed to write to UHF module\n");
        return ERROR_INTERNAL;
    }
    vTaskDelay(pdMS_TO_TICKS(1)); // Delay 1ms because SPI operations are high-priority and may block other tasks from executing
    return SUCCESS;
}

status_t writeRegister_chk(uint8_t address, uint8_t value) {
    status_t result = writeRegister(address, value);
    if (result != SUCCESS) {
        return result;
    }
    // Check to make sure this actually worked
    uint8_t read_value = 0;
    result = readRegister(address, &read_value);
    if (result != SUCCESS) {
        return result;
    }
    if (read_value != value) {
        return ERROR_WRITE_FAILED;
    }
    return SUCCESS;
}

status_t uhf_init(uint32_t frequency) {
    spi_m_sync_enable(&UHF_SERCOM);

    // Set CS high
    gpio_set_pin_level(UHF_CS, true);

    // Reset UHF module by driving the reset pin low and then high again
    gpio_set_pin_level(UHF_RST, true);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_pin_level(UHF_RST, false);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_pin_level(UHF_RST, true);
    vTaskDelay(pdMS_TO_TICKS(50));

    // SPI is already started

    // check version
    uint8_t version = 0;
    status_t reg_result = readRegister(REG_VERSION, &version);
    if (version != 0x12) {
        warning("UHF module not found\n");
        return ERROR_INTERNAL;
    } else {
        info("UHF module found!\n");
    }

    // Put UHF radio in sleep mode
    reg_result |= writeRegister_chk(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP);

    if (reg_result == SUCCESS) {
        debug("UHF module put to sleep\n");
    } else {
        warning("Failed to put UHF module to sleep! [Error: %d]\n", reg_result);
    }

    // set frequency to the frequency argument
    uint64_t frf = ((uint64_t)frequency << 19) / 32000000;
    reg_result |= writeRegister_chk(REG_FRF_MSB, (uint8_t)(frf >> 16));
    reg_result |= writeRegister_chk(REG_FRF_MID, (uint8_t)(frf >> 8));
    reg_result |= writeRegister_chk(REG_FRF_LSB, (uint8_t)(frf >> 0));

    if (reg_result == SUCCESS) {
        debug("Frequency written succesfully\n");
    } else {
        warning("Failed to write frequency! [Error: %d]\n", reg_result);
    }

    // set base addresses
    reg_result |= writeRegister_chk(REG_FIFO_TX_BASE_ADDR, 0);
    reg_result |= writeRegister_chk(REG_FIFO_RX_BASE_ADDR, 0);

    if (reg_result == SUCCESS) {
        debug("Base addresses written succesfully\n");
    } else {
        warning("Failed to write base addresses! [Error: %d]\n", reg_result);
    }

    // set LNA boost
    uint8_t reg_lna_value = 0;
    reg_result |= readRegister(REG_LNA, &reg_lna_value);
    reg_result |= writeRegister_chk(REG_LNA, reg_lna_value | 0x03);

    // set auto AGC
    reg_result |= writeRegister_chk(REG_MODEM_CONFIG_3, 0x04);

    // set output power to 17 dBm
    // setTxPower(17); //TODO this seems complex and I haven't implemented it yet

    // put in standby mode
    reg_result |= idle();

    if (reg_result != SUCCESS) {
        return ERROR_INTERNAL;
    }
    return SUCCESS;
}

status_t uhf_send(uint8_t *data, uint8_t length) {
    debug("Sending UHF packet of length %d\n", length);
    status_t status = begin_packet();
    if (status != SUCCESS) {
        return status;
    }

    return ERROR_NOT_YET_IMPLEMENTED;
}

status_t begin_packet() {
    if (is_transmitting()) {
        return ERROR_BUSY;
    }

    // put in standby mode
    status_t reg_result = idle();

    // set FIFO pointers
    reg_result = writeRegister_chk(REG_FIFO_ADDR_PTR, 0);
    if (reg_result != SUCCESS) {
        return reg_result;
    }

    // clear FIFO
    reg_result = writeRegister_chk(REG_FIFO_TX_BASE_ADDR, 0);
    if (reg_result != SUCCESS) {
        return reg_result;
    }

    return SUCCESS;
}

bool isTransmitting() {
    uint8_t mode = 0;
    status_t reg_result = readRegister(REG_OP_MODE, &mode);
    if ((mode & MODE_TX) == MODE_TX) {
        return true;
    }

    uint8_t irq_flags = 0;
    reg_result |= readRegister(REG_IRQ_FLAGS, &irq_flags);
    if (irq_flags & IRQ_TX_DONE_MASK) {
        // clear IRQ's
        writeRegister(REG_IRQ_FLAGS, IRQ_TX_DONE_MASK); // Don't use the chk version here, because this may not be a persistent register
    }

    return false;
}

status_t idle() {
    return writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
}