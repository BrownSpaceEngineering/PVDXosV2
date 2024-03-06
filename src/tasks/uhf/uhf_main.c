#include "hal_spi_m_os.h"
#include "uhf_task.h"

#include <stdbool.h>

struct uhfTaskMemory uhfMem = {0};

struct spi_m_os_descriptor spi_descriptor;
struct spi_m_os_descriptor *const p_spi_descriptor = &spi_descriptor;
struct io_descriptor *p_uhf_io = &(spi_descriptor.io);

int UHF_init(uint32_t frequency);

void print_wires() {
    info("----------------\n");
    uint8_t UHF_CS_Level = gpio_get_pin_level(UHF_CS);
    info("UHF_CS Level: %d\n", UHF_CS_Level);
    uint8_t UHF_Interrupt_Level = gpio_get_pin_level(UHF_IRQ);
    info("UHF_Interrupt Level: %d\n", UHF_Interrupt_Level);

    uint8_t UHF_CIPO_Level = gpio_get_pin_level(UHF_CIPO);
    uint8_t UHF_COPI_Level = gpio_get_pin_level(UHF_COPI);
    uint8_t UHF_CLK_Level = gpio_get_pin_level(UHF_SCK);
    uint8_t UHF_RST_Level = gpio_get_pin_level(UHF_RST);

    info("UHF_CIPO Level: %d\n", UHF_CIPO_Level);
    info("UHF_COPI Level: %d\n", UHF_COPI_Level);
    info("UHF_CLK Level: %d\n", UHF_CLK_Level);
    info("UHF_RST Level: %d\n", UHF_RST_Level);
    info("----------------\n");
}

void uhf_main(void *pvParameters) {
    info("UHF task started!\n");
    uint16_t data __attribute__((unused));
    spi_m_os_get_io_descriptor(&SPI_0, &p_uhf_io);
    /* Control the slave select(SS) pin */
    gpio_set_pin_level(UHF_CS, false);
    spi_m_os_init(p_spi_descriptor, &SPI_0);

    spi_m_os_enable(p_spi_descriptor);

    info("UHF RTOS API Initialized\n");

    // Initialize the UHF hardware

    for (;;) {
        if (UHF_init(435E6) == 1) {
            info("UHF Initialized (kinda)\n");
            break;
        }
        break;
        // print_wires()
        //  vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // The NVIC must be configured so that SPI interrupt requests are periodically serviced
    // io_read(io, (uint8_t *)&data, 2);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

uint8_t readRegisterBitBang(uint8_t address) {
    uint8_t register_addr = address & 0x7F; // All bits except the first one

    // Set CS low
    gpio_set_pin_level(UHF_CS, false);
    for (int i = 0; i < 8; i++) {
        // Set value on COPI
        uint8_t bit_to_send = (register_addr & 0x80) >> 7;
        gpio_set_pin_level(BITBANG_COPI, bit_to_send);
        // wait for the line to stabilize
        for (int z = 0; z < 255; z++) {
            asm("");
        }
        // Set clock high
        gpio_set_pin_level(BITBANG_SCK, true);
        // wait again
        for (int z = 0; z < 255; z++) {
            asm("");
        }
        info("Writing bit %d to COPI: %d\n", i, bit_to_send);
        print_wires();
        // Set clock low
        gpio_set_pin_level(BITBANG_SCK, false);
        // Shift register address left
        register_addr <<= 1;
    }

    // Finished writing, now read 8 bytes

    // Wait a bit first
    for (int z = 0; z < 255; z++) {
        asm("");
    }

    info("-- Switching to read --\n");

    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        // Read data from CIPO, then pulse the clock
        uint8_t pin_value = gpio_get_pin_level(BITBANG_CIPO);
        if (pin_value > 1) {
            warning("oops that's bad\n");
        }
        data = data << 1;
        data = data | pin_value;
        info("Reading bit %d from CIPO: %d\n", i, pin_value);
        print_wires();

        // Set clock high
        gpio_set_pin_level(BITBANG_SCK, true);

        // wait again
        for (int z = 0; z < 255; z++) {
            asm("");
        }

        // Set clock low
        gpio_set_pin_level(BITBANG_SCK, false);

        // wait again
        for (int z = 0; z < 255; z++) {
            asm("");
        }
    }

    info("Read from UHF module: %d\n", data);
    return data;
}

uint8_t readRegisterASF(uint8_t address) {
    uint8_t register_addr = address & 0x7F; // All bits except the first one
    uint8_t write_result = io_write(p_uhf_io, &register_addr, 1);
    if (write_result != 1) {
        warning("Failed to write to UHF module\n");
    }

    // wait a little
    for (int z = 0; z < 255; z++) {
        asm("");
    }

    uint8_t data = 0xCC;
    uint8_t read_result = io_read(p_uhf_io, &data, 1);
    if (read_result != 1) {
        warning("Failed to read from UHF module\n");
        return 0x0;
    }
    info("Read from UHF module: %d\n", data);
    return data;
}

uint8_t readRegister(uint8_t address) {
    // return readRegisterASF(address);
    return readRegisterBitBang(address);
}

/*
void writeRegister(uint8_t address) {
    uint8_t register_addr = address & 0x7F; // All bits except the first one
    register_addr |= 0x80;                  // Set the first bit high
    int32_t write_result = io_write(p_uhf_io, &register_addr, 1);
    if (write_result != 1) {
        warning("Failed to write to UHF module\n");
    }
    uint8_t data = 0xCC;
    int32_t read_result = io_read(p_uhf_io, &data, 1);
    if (read_result != 1) {
        warning("Failed to read from UHF module\n");
        return 0x0;
    } else {
        info("Read from UHF module: %d\n", data);
        return data;
    }
}
*/

int UHF_init(uint32_t frequency) {
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
    gpio_set_pin_level(UHF_CS, false); // Set CS low
    uint8_t version = readRegister(REG_VERSION);
    gpio_set_pin_level(UHF_CS, true); // Set CS high
    if (version != 0x12) {
        warning("UHF module not found\n");
        return 0;
    } else {
        info("UHF module found!\n");
    }

    /*
    // put in sleep mode
    sleep();

    // set frequency
    setFrequency(frequency);

    // set base addresses
    writeRegister(REG_FIFO_TX_BASE_ADDR, 0);
    writeRegister(REG_FIFO_RX_BASE_ADDR, 0);

    // set LNA boost
    writeRegister(REG_LNA, readRegister(REG_LNA) | 0x03);

    // set auto AGC
    writeRegister(REG_MODEM_CONFIG_3, 0x04);

    // set output power to 17 dBm
    setTxPower(17);

    // put in standby mode
    idle();
    */

    return 1;
}