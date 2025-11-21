/*                          
  _ __      _ _   __ _    _ __   
 | '  \    | '_| / _` |  | '  \  
 |_|_|_|  _|_|_  \__,_|  |_|_|_| 
_|"""""|_|"""""|_|"""""|_|"""""| 
"`-0-0-'"`-0-0-'"`-0-0-'"`-0-0-' 

 * EM008LX MRAM Test - Metro Grand Central M4 (SAMD51)
 * Using Atmel START SPI driver (spi_m_sync)
 *
 * Features:
 * - Reads Device ID
 * - Disables Block Protection
 * - Writes/Reads/Verifies Data
 * - Reads and decodes Status Flag Register

 Considerations:
 - using triplicated variables for important stuff
 - block protection?
 */

#include "atmel_start.h"
#include "hal_gpio.h"
#include "hal_delay.h"

// MRAM Commands (we should cross check)
#define CMD_WREN  0x06
#define CMD_WRDI  0x04
#define CMD_RDSR  0x05
#define CMD_WRSR  0x01
#define CMD_READ  0x03
#define CMD_WRITE 0x02
#define CMD_RDID  0x9F
#define CMD_RDFR  0x70  // Read Status Flag Register

// Test parameters
#define TEST_ADDRESS 0x0000
#define TEST_DATA    0xA5

// ---------------------- SPI Helpers ----------------------

static inline void mramSelect(void)   { gpio_set_pin_level(MRAM1_CS, false); }
static inline void mramDeselect(void) { gpio_set_pin_level(MRAM1_CS, true); }

uint8_t spiXfer(uint8_t val) {
    struct spi_xfer xfer;
    uint8_t rx;
    xfer.txbuf = &val;
    xfer.rxbuf = &rx;
    xfer.size = 1;
    spi_m_sync_transfer(&SPI_MRAM, &xfer);
    return rx;
}

void spiWrite(const uint8_t *data, uint32_t len) {
    struct spi_xfer xfer = {
        .txbuf = (uint8_t*)data,
        .rxbuf = NULL,
        .size  = len
    };
    spi_m_sync_transfer(&SPI_MRAM, &xfer);
}

void spiRead(uint8_t *data, uint32_t len) {
    struct spi_xfer xfer = {
        .txbuf = NULL,
        .rxbuf = data,
        .size  = len
    };
    spi_m_sync_transfer(&SPI_MRAM, &xfer);
}

// ---------------------- Core MRAM Operations ----------------------

void writeEnable(void) {
    uint8_t cmd = CMD_WREN;
    mramSelect();
    spiWrite(&cmd, 1);
    mramDeselect();
}

uint8_t readStatus(void) {
    uint8_t cmd = CMD_RDSR;
    uint8_t val = 0;
    mramSelect();
    spiWrite(&cmd, 1);
    spiRead(&val, 1);
    mramDeselect();
    return val;
}

void writeStatus(uint8_t value) {
    uint8_t buf[2] = {CMD_WRSR, value};
    writeEnable();
    mramSelect();
    spiWrite(buf, 2);
    mramDeselect();
}

uint8_t readByte(uint32_t address) {
    uint8_t cmd[4] = {
        CMD_READ,
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)(address)
    };
    uint8_t val = 0;
    mramSelect();
    spiWrite(cmd, 4);
    spiRead(&val, 1);
    mramDeselect();
    return val;
}

void readBytes(uint32_t address, uint8_t *data, uint32_t size) {
    uint8_t cmd[4] = {
        CMD_READ,
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)(address)
    };
    mramSelect();
    spiWrite(cmd, 4);
    spiRead(data, size);
    mramDeselect();
}

void writeByte(uint32_t address, uint8_t data) {
    uint8_t buf[5] = {
        CMD_WRITE,
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)(address),
        data
    };
    writeEnable();
    mramSelect();
    spiWrite(buf, 5);
    mramDeselect();
}

void writeBytes(uint32_t address, const uint8_t *data, uint32_t size) {
    uint8_t hdr[4] = {
        CMD_WRITE,
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)(address)
    };
    writeEnable();
    mramSelect();
    spiWrite(hdr, 4);
    spiWrite(data, size);
    mramDeselect();
}

// ---------------------- Status Flag Register ----------------------

uint8_t readStatusFlag(void) {
    uint8_t cmd = CMD_RDFR;
    uint8_t flag = 0;
    mramSelect();
    spiWrite(&cmd, 1);
    spiRead(&flag, 1);
    mramDeselect();
    return flag;
}

// ---------------------- Tests ----------------------

void testReadID(void) {
    uint8_t cmd = CMD_RDID;
    uint8_t id[3] = {0};
    mramSelect();
    spiWrite(&cmd, 1);
    spiRead(id, 3);
    mramDeselect();

    if ((id[0] == 0xFF && id[1] == 0xFF) || (id[0] == 0x00 && id[1] == 0x00)) {
        // fatal error: invalid device ID
    }
}

void disableBlockProtection(void) {
    uint8_t status = readStatus();

    if (status & 0x0C) {
        // disable block protection
        writeStatus(status & ~0x0C);
    }

    status = readStatus();
    if ((status & 0x0C) != 0) {
        // fatal error: block protection not disabled
    }
}

void testWritesReads(uint32_t addr, int salt) {
    const uint32_t NUM_BYTES = 256;
    uint8_t send_data[NUM_BYTES];
    uint8_t recv_data[NUM_BYTES];

    for (uint32_t i = 0; i < NUM_BYTES; i++)
        send_data[i] = (uint8_t)((i + salt) % 100);

    writeBytes(addr, send_data, NUM_BYTES);
    readBytes(addr, recv_data, NUM_BYTES);

    for (uint32_t i = 0; i < NUM_BYTES; i++) {
        if (recv_data[i] != send_data[i]) {
            // fatal error: test failed
            return;
        }
    }
}

// ---------------------- Main ----------------------

void mram_init(void) {
    atmel_start_init();
    spi_m_sync_enable(&SPI_MRAM);

    gpio_set_pin_level(MRAM1_CS, true);

    delay_ms(50);

    testReadID();
    disableBlockProtection();
    for (int i = 0; i < 100; i++) {
        testWritesReads(0x000100, i+3);
        testWritesReads(0x000300, i+7);
    }

    while (1) {
        delay_ms(1000);
    }
}
