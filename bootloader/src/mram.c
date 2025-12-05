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
#define CMD_RDNVOL 0xB5
#define CMD_RDVOL 0x85
#define CMD_WRVOL 0x81

// Test parameters
#define TEST_ADDRESS 0x0000
#define TEST_DATA    0xA5

// ---------------------- SPI Helpers ----------------------

static inline void mram_select(void)   { gpio_set_pin_level(MRAM1_CS, false); }
static inline void mram_deselect(void) { gpio_set_pin_level(MRAM1_CS, true); }

void mram_fatal(void) { while (1); }

void spi_write(const uint8_t *data, uint32_t len) {
    struct spi_xfer xfer = {
        .txbuf = (uint8_t*)data,
        .rxbuf = NULL,
        .size  = len
    };
    spi_m_sync_transfer(&SPI_MRAM, &xfer);
}

void spi_read(uint8_t *data, uint32_t len) {
    struct spi_xfer xfer = {
        .txbuf = NULL,
        .rxbuf = data,
        .size  = len
    };
    spi_m_sync_transfer(&SPI_MRAM, &xfer);
}

// ---------------------- Core MRAM Operations ----------------------

void write_enable(void) {
    uint8_t cmd = CMD_WREN;
    mram_select();
    spi_write(&cmd, 1);
    mram_deselect();
}

uint8_t read_status(void) {
    uint8_t cmd = CMD_RDSR;
    uint8_t val = 0;
    mram_select();
    spi_write(&cmd, 1);
    spi_read(&val, 1);
    mram_deselect();
    return val;
}

void write_status(uint8_t value) {
    uint8_t buf[2] = {CMD_WRSR, value};
    write_enable();
    mram_select();
    spi_write(buf, 2);
    mram_deselect();
}

void mram_read_bytes(uint32_t address, uint8_t *data, uint32_t size) {
    uint8_t cmd[4] = {
        CMD_READ,
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)(address)
    };
    mram_select();
    spi_write(cmd, 4);
    spi_read(data, size);
    mram_deselect();
}

void mram_write_bytes(uint32_t address, const uint8_t *data, uint32_t size) {
    uint8_t hdr[4] = {
        CMD_WRITE,
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)(address)
    };
    write_enable();
    mram_select();
    spi_write(hdr, 4);
    spi_write(data, size);
    mram_deselect();
}

void check_device_id(void) {
    uint8_t cmd = CMD_RDID;
    uint8_t id[3] = {0};
    mram_select();
    spi_write(&cmd, 1);
    spi_read(id, 3);
    mram_deselect();

    if (id[0] != 0x6b || id[1] != 0xbb || id[2] != 0x14) {
        // invalid device ID
        mram_fatal();
    }
}

uint8_t read_nonvol_reg(void) {
    uint8_t cmd[4] = {
        CMD_RDNVOL,
        0,
        0,
        8
    };
    uint8_t reg_val = 0;
    mram_select();
    spi_write(cmd, 4);
    spi_read(&reg_val, 1);
    mram_deselect();

    return reg_val;
}

uint8_t read_vol_reg(void) {
    uint8_t cmd[4] = {
        CMD_RDVOL,
        0,
        0,
        8
    };
    uint8_t reg_val = 0;
    mram_select();
    spi_write(cmd, 4);
    spi_read(&reg_val, 1);
    mram_deselect();

    return reg_val;
}

void write_vol_reg(uint8_t reg_val) {
    uint8_t cmd[4] = {
        CMD_WRVOL,
        0,
        0,
        8
    };
    write_enable();
    mram_select();
    spi_write(cmd, 4);
    spi_write(&reg_val, 1);
    mram_deselect();
}

void set_persistent_mode(void) {
    uint8_t reg_val = read_nonvol_reg();

    if (!(reg_val & 0x03)) {
        reg_val |= 0x03;
        write_vol_reg(reg_val);
    }

    reg_val = read_vol_reg();
    if (!(reg_val & 0x03)) {
        // persistent mode not enabled
        mram_fatal();
    }
}

// ---------------------- Tests ----------------------

void disable_block_protection(void) {
    uint8_t status = read_status();

    if (status & 0x0C) {
        // disable block protection
        write_status(status & ~0x0C);
    }

    status = read_status();
    if ((status & 0x0C) != 0) {
        // block protection not disabled
        mram_fatal();
    }
}

void test_writes_reads(uint32_t addr, int salt) {
    const uint32_t NUM_BYTES = 512;
    uint8_t send_data[NUM_BYTES];
    uint8_t recv_data[NUM_BYTES];

    for (uint32_t i = 0; i < NUM_BYTES; i++)
        send_data[i] = (uint8_t)((i + salt) % 100);

    mram_write_bytes(addr, send_data, NUM_BYTES);
    mram_read_bytes(addr, recv_data, NUM_BYTES);

    for (uint32_t i = 0; i < NUM_BYTES; i++) {
        if (recv_data[i] != send_data[i]) {
            // test failed
            mram_fatal();
        }
    }
}

// ---------------------- Main ----------------------

void mram_init(void) {
    atmel_start_init();
    spi_m_sync_enable(&SPI_MRAM);

    gpio_set_pin_level(MRAM1_CS, true);

    delay_ms(50);

    check_device_id();
    disable_block_protection();
    set_persistent_mode();
    for (int i = 0; i < 100; i++) {
        test_writes_reads(0x000980, i+3);
        test_writes_reads(0x000f00, i+7);
    }

    while (1) {
        delay_ms(1000);
    }
}
