/*
 * EM008LX MRAM Test - Metro Grand Central M4 (SAMD51)
 * Using Atmel START SPI driver (spi_m_sync)
 *
 * Features:
 * - Reads Device ID
 * - Disables Block Protection
 * - Writes/Reads/Verifies Data
 * - Reads and decodes Status Flag Register
 */

#include "atmel_start.h"
#include "hal_gpio.h"
#include "hal_delay.h"
#include "logging.h"

// External SPI descriptor
// extern struct spi_m_sync_descriptor SPI_MRAM;

// Pin definition for MRAM chip select
// #define MRAM1_CS GPIO(GPIO_PORTC, 4)

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

void printStatusFlagDetails(uint8_t flag) {
    info_impl("Status Flag: 0x%02X\r\n", flag);
    info_impl("  RDY/BUSY: %s\r\n", (flag & 0x80) ? "1 (Ready)" : "0 (Busy)");
    info_impl("  E_ERR:    %s\r\n", (flag & 0x40) ? "1 (Erase Error)" : "0");
    info_impl("  P_ERR:    %s\r\n", (flag & 0x20) ? "1 (Program Error)" : "0");
    if (flag & 0x20) info_impl("Program Error detected!\r\n");
    if (flag & 0x40) info_impl("Erase Error detected!\r\n");
    if (!(flag & 0x80)) info_impl("  ⏳ Device still busy!\r\n");
}

void checkStatusFlagIfError(void) {
    uint8_t flag = readStatusFlag();
    info_impl("\n[DEBUG] Status Flag Register:\r\n");
    printStatusFlagDetails(flag);
}

// ---------------------- Tests ----------------------

void testReadID(void) {
    uint8_t cmd = CMD_RDID;
    uint8_t id[3] = {0};
    mramSelect();
    spiWrite(&cmd, 1);
    spiRead(id, 3);
    mramDeselect();

    info_impl("\n[TEST] Device ID:\r\n");
    info_impl("  Manufacturer: 0x%02X\r\n", id[0]);
    info_impl("  Memory Type:  0x%02X\r\n", id[1]);
    info_impl("  Capacity:     0x%02X\r\n", id[2]);

    if ((id[0] == 0xFF && id[1] == 0xFF) || (id[0] == 0x00 && id[1] == 0x00))
        info_impl("  Check wiring or level shifter connections!\r\n");
    else
        info_impl("  Device responded correctly.\r\n");
}

void disableBlockProtection(void) {
    uint8_t status = readStatus();
    info_impl("\n[INFO] Current Status Register: 0x%02X\r\n", status);

    if (status & 0x0C) {
        info_impl("  Disabling block protection...\r\n");
        writeStatus(status & ~0x0C);
    }

    status = readStatus();
    info_impl("  New Status Register: 0x%02X\r\n", status);
    if ((status & 0x0C) == 0)
        info_impl("   Block protection disabled.\r\n");
    else
        info_impl("  Block protection still active!\r\n");
}

void testWriteRead(void) {
    info_impl("\n[TEST] Write/Read Single Address\r\n");

    info_impl("  Writing 0x%02X to address 0x%06X\r\n", TEST_DATA, TEST_ADDRESS);

    writeByte(TEST_ADDRESS, TEST_DATA);
    uint8_t data = readByte(TEST_ADDRESS);

    info_impl("  Read back: 0x%02X\r\n", data);

    if (data == TEST_DATA)
        info_impl("  Match OK!\r\n");
    else {
        info_impl("  Data mismatch!\r\n");
        checkStatusFlagIfError();
    }
}

void testWritesReads(uint32_t addr) {
    info_impl("\n[TEST] Write/Read Multiple Addresses At Once\r\n");

    const uint32_t NUM_BYTES = 10000;
    uint8_t send_data[NUM_BYTES];
    uint8_t recv_data[NUM_BYTES];

    for (uint32_t i = 0; i < NUM_BYTES; i++)
        send_data[i] = (uint8_t)(i % 256);

    info_impl("  Writing %lu bytes to address 0x%06lX\r\n", NUM_BYTES, addr);

    writeBytes(addr, send_data, NUM_BYTES);
    readBytes(addr, recv_data, NUM_BYTES);

    for (uint32_t i = 0; i < NUM_BYTES; i++) {
        if (recv_data[i] != send_data[i]) {
            info_impl("  Data mismatch at %lu (expected 0x%02X, got 0x%02X)\r\n",
                   i, send_data[i], recv_data[i]);
            checkStatusFlagIfError();
            return;
        }
    }

    info_impl("  Match OK!\r\n");
}

// ---------------------- Main ----------------------

void mram_main(void) {
    // atmel_start_init();
    spi_m_sync_enable(&SPI_MRAM);

    gpio_set_pin_level(MRAM1_CS, true);

    delay_ms(50);
    info_impl("\n=== EM008LX MRAM Test - Metro Grand Central M4 ===\r\n");

    testReadID();
    disableBlockProtection();
    testWriteRead();
    testWritesReads(0x000100);

    info_impl("\nAll tests complete.\r\n");

    while (1) {
        delay_ms(1000);
    }
}
