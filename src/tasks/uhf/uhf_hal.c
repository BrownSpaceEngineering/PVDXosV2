#include "uhf_hal.h"

#include "pincfg.h"

#define UHF_SPI_BUFFER_CAPACITY 8

uint8_t uhf_tx_buffer[UHF_SPI_BUFFER_CAPACITY] = {0};
uint8_t uhf_rx_buffer[UHF_SPI_BUFFER_CAPACITY] = {0};
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
    uint32_t bytes_transferred = spi_m_sync_transfer(&UHF_SERCOM, &uhf_transfer_struct);
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
    uint32_t bytes_transferred = spi_m_sync_transfer(&UHF_SERCOM, &uhf_transfer_struct);
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
    uhf_set_tx_boost_power();
    // setTxPower(17); //TODO this seems complex and I haven't implemented it yet

    // put in standby mode
    reg_result |= idle();

    if (reg_result != SUCCESS) {
        return ERROR_INTERNAL;
    }
    return SUCCESS;
}

status_t uhf_send(uint8_t *data, size_t length) {
    debug("Sending UHF packet of length %d\n", length);
    if (length > MAX_PKT_LENGTH) {
        return ERROR_MAX_SIZE_EXCEEDED;
    }
    status_t status = uhf_begin_packet();
    if (status != SUCCESS) {
        return status;
    }
    status |= uhf_write(data, length);

    status |= uhf_end_packet(); // also sends the message
    return status;
}

void uhf_set_tx_boost_power() {
    // Set to boost mode
    int PA_Select = 1;         // Set boost enabled
    int Max_Power = 0x4;       // IDK
    int Output_Power = 0b1111; // Max output power = 17 (17 - (15 - Output_Power))
    uint8_t PA_Config = 0;
    PA_Config |= (PA_Select << 7);      // set PA_BOOST
    PA_Config |= (Max_Power << 4);      // set Max_Power
    PA_Config |= (Output_Power & 0x0F); // set Output_Power
    writeRegister(REG_PA_CONFIG, PA_Config);
    writeRegister(REG_PA_DAC, 0x87);
    setOCP(140);
}

void setOCP(uint8_t mA) {
    uint8_t ocpTrim = 27;

    if (mA <= 120) {
        ocpTrim = (mA - 45) / 5;
    } else if (mA <= 240) {
        ocpTrim = (mA + 30) / 10;
    }

    writeRegister(REG_OCP, 0x20 | (0x1F & ocpTrim));
}

status_t uhf_end_packet() {

    // Original LoRa code included this check, but assuming we're not doing async mode, we can remove it.
    /*
    if ((async) && (_onTxDone)) { writeRegister(REG_DIO_MAPPING_1, 0x40); } // DIO0 => TXDONE
    */

    // put in TX mode
    info("Beginning UHF transmission... (time: %d)\n", xTaskGetTickCount());
    status_t reg_status = writeRegister_chk(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_TX);

    // if (!async) { Original LoRa code included this check, but assuming we're not doing async mode, we can remove it.
    info("Waiting for UHF transmission to complete...\n");
    // wait for TX done
    uint8_t irq_flags = 0;
    while (1) {
        readRegister(REG_IRQ_FLAGS, &irq_flags);
        if ((irq_flags & IRQ_TX_DONE_MASK) == 0) {
            // TX not done yet, wait a bit
            vTaskDelay(pdMS_TO_TICKS(UHF_TX_DONE_POLLING_RATE_MS));
        } else {
            // TX done!
            break;
        }
    }
    // clear IRQ's
    reg_status |= writeRegister(REG_IRQ_FLAGS, IRQ_TX_DONE_MASK);

    if (reg_status == SUCCESS) {
        info("UHF transmission successful! (time: %d)\n", xTaskGetTickCount());
    }
    return reg_status;
}

status_t uhf_write(uint8_t *data, size_t size) {
    uint8_t currentLength = 0;
    status_t reg_status = readRegister(REG_PAYLOAD_LENGTH, &currentLength);

    // check size
    if ((currentLength + size) > MAX_PKT_LENGTH) {
        return ERROR_MAX_SIZE_EXCEEDED;

        // Original implementation truncates: \/
        // size = MAX_PKT_LENGTH - currentLength;
    }

    // write data
    for (size_t i = 0; i < size; i++) {
        reg_status |= writeRegister(REG_FIFO, data[i]); // Not a persistent register, so we don't use the chk version
    }

    // update length
    reg_status |= writeRegister_chk(REG_PAYLOAD_LENGTH, currentLength + size);

    return reg_status;
}

status_t uhf_begin_packet() {
    if (is_transmitting()) {
        return ERROR_BUSY;
    }

    // put in standby mode
    status_t reg_result = idle();

    // Currently, supporting only explicit header mode (original code allows for implicit header mode config here)
    /*if (implicitHeader) { implicitHeaderMode(); } else { explicitHeaderMode(); }*/
    reg_result |= explicitHeaderMode();

    // reset FIFO address and paload length
    reg_result |= writeRegister(REG_FIFO_ADDR_PTR, 0);
    reg_result |= writeRegister(REG_PAYLOAD_LENGTH, 0);

    return SUCCESS;
}

bool is_transmitting() {
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

status_t explicitHeaderMode() {
    uint8_t reg_modem_config_1_val = 0;
    status_t reg_result = readRegister(REG_MODEM_CONFIG_1, &reg_modem_config_1_val);
    reg_result |= writeRegister_chk(REG_MODEM_CONFIG_1, reg_modem_config_1_val & 0xfe);
    return reg_result;
}

status_t idle() {
    return writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
}