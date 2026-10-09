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

#include "mram_driver.h"
#include "watchdog_driver.h"

// MRAM Command Bytes
#define CMD_WREN    0x06    // Write Enable
#define CMD_RDSR    0x05    // Read Status Register
#define CMD_WRSR    0x01    // Write Status Register
#define CMD_READ    0x03    // Read Data
#define CMD_WRITE   0x02    // Write Data
#define CMD_RDID    0x9F    // Read Device ID
#define CMD_RDNVOL  0xB5    // Read Nonvolatile Register
#define CMD_RDVOL   0x85    // Read Volatile Register
#define CMD_WRVOL   0x81    // Write Volatile Register

#define PMM_REG     8       // Persistent Memory Mode register

typedef enum {
    MRAM_INIT_OK = 0,                        // No issues
    MRAM_INIT_BAD_ID = 1u << 1,              // Device ID does not match expected value
    MRAM_INIT_FAIL_BLOCK_PROT = 1u << 2,     // Block protection not disabled
    MRAM_INIT_FAIL_PERSIST = 1u << 3,        // Persistence mode not enabled
} mram_init_status_t;

// ---------------------- SPI Helpers ----------------------

/**
 * @brief Halts execution and waits for the watchdog to reboot the system.
 * @warning Should only be called when a reboot will definitely fix the fatal
 *          state. If the fatal state is unrecoverable, use
 *          `mram_complete_fatal` instead.
 */
void mram_recoverable_fatal(void) { while (1); }

/**
 * @brief Sets the MRAM state to fatal to prevent functions from using MRAM
 *        inappropriately. Does not reboot the system.
 */
void mram_complete_fatal(void) {
    /**
     * TODO: Figure out what to do in case of unrecoverable failure.
     * Consider a function bool mram_is_fatal(void) that returns the status;
     * every exposed function should call that function and only proceed if the
     * result is false.
     */
}

static inline void mram_select(uint8_t mram) {
    if (mram == 1) {
        gpio_set_pin_level(MRAM1_CS, false);
    } else if (mram == 2) {
        gpio_set_pin_level(MRAM2_CS, false);
    } else if (mram == 3) {
        gpio_set_pin_level(MRAM3_CS, false);
    } else {
        mram_recoverable_fatal();
    }
}

static inline void mram_deselect(uint8_t mram) {
    if (mram == 1) {
        gpio_set_pin_level(MRAM1_CS, true);
    } else if (mram == 2) {
        gpio_set_pin_level(MRAM2_CS, true);
    } else if (mram == 3) {
        gpio_set_pin_level(MRAM3_CS, true);
    } else {
        mram_recoverable_fatal();
    }
}

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

// ---------------------- MRAM Commands ----------------------

void write_enable(uint8_t mram) {
    uint8_t cmd = CMD_WREN;
    mram_select(mram);
    spi_write(&cmd, 1);
    mram_deselect(mram);
}

uint8_t read_status(uint8_t mram) {
    uint8_t cmd = CMD_RDSR;
    uint8_t val = 0;
    mram_select(mram);
    spi_write(&cmd, 1);
    spi_read(&val, 1);
    mram_deselect(mram);
    return val;
}

void write_status(uint8_t mram, uint8_t value) {
    uint8_t buf[2] = {CMD_WRSR, value};
    write_enable(mram);
    mram_select(mram);
    spi_write(buf, 2);
    mram_deselect(mram);
}

void read_bytes(uint8_t mram, uint32_t address, uint8_t *data, uint32_t size) {
    watchdog_pet();
    
    uint8_t cmd[4] = {
        CMD_READ,
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)(address)
    };
    mram_select(mram);
    spi_write(cmd, 4);
    spi_read(data, size);
    mram_deselect(mram);

    watchdog_pet();
}

void write_bytes(uint8_t mram, uint32_t address, const uint8_t *data, uint32_t size) {
    watchdog_pet();

    uint8_t hdr[4] = {
        CMD_WRITE,
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)(address)
    };
    write_enable(mram);
    mram_select(mram);
    spi_write(hdr, 4);
    spi_write(data, size);
    mram_deselect(mram);

    watchdog_pet();
}

uint8_t read_nonvol_reg(uint8_t mram, uint8_t reg) {
    uint8_t cmd[4] = {
        CMD_RDNVOL,
        0,
        0,
        reg
    };
    uint8_t reg_val = 0;
    mram_select(mram);
    spi_write(cmd, 4);
    spi_read(&reg_val, 1);
    mram_deselect(mram);
    return reg_val;
}

uint8_t read_vol_reg(uint8_t mram, uint8_t reg) {
    uint8_t cmd[4] = {
        CMD_RDVOL,
        0,
        0,
        reg
    };
    uint8_t reg_val = 0;
    mram_select(mram);
    spi_write(cmd, 4);
    spi_read(&reg_val, 1);
    mram_deselect(mram);
    return reg_val;
}

void write_vol_reg(uint8_t mram, uint8_t reg, uint8_t reg_val) {
    uint8_t cmd[4] = {
        CMD_WRVOL,
        0,
        0,
        reg
    };
    write_enable(mram);
    mram_select(mram);
    spi_write(cmd, 4);
    spi_write(&reg_val, 1);
    mram_deselect(mram);
}


// ---------------------- Core Operations ----------------------

void mram_init_report_err(uint8_t mram, mram_init_status_t status) {
    /** TODO: figure out what to do when reporting an error. */
}

mram_init_status_t check_device_id(uint8_t mram) {
    uint8_t cmd = CMD_RDID;
    uint8_t id[3] = {0};
    mram_select(mram);
    spi_write(&cmd, 1);
    spi_read(id, 3);
    mram_deselect(mram);

    if (id[0] != 0x6B || id[1] != 0xBB || id[2] != 0x14) {
        // invalid device ID
        return MRAM_INIT_BAD_ID;
    } else {
        return MRAM_INIT_OK;
    }
}

mram_init_status_t disable_block_protection(uint8_t mram) {
    uint8_t status = read_status(mram);

    if (status & 0x0C) {
        // disable block protection
        write_status(mram, status & ~0x0C);
    }

    status = read_status(mram);
    if ((status & 0x0C) != 0) {
        // block protection not disabled
        return MRAM_INIT_FAIL_BLOCK_PROT;
    } else {
        return MRAM_INIT_OK;
    }
}

mram_init_status_t set_persistent_mode(uint8_t mram) {
    uint8_t reg_val = read_nonvol_reg(mram, PMM_REG);

    if (!(reg_val & 0x03)) {
        reg_val |= 0x03;
        write_vol_reg(mram, PMM_REG, reg_val);
    }

    reg_val = read_vol_reg(mram, PMM_REG);
    if (!(reg_val & 0x03)) {
        // persistent mode not enabled
        return MRAM_INIT_FAIL_PERSIST;
    } else {
        return MRAM_INIT_OK;
    }
}

/**
 * @brief Writes data to all MRAM modules.
 * @param address Destination address.
 * @param data Data to be written.
 * @param size Size of data to be written, in bytes.
 */
void mram_write_bytes(uint32_t address, const uint8_t *data, uint32_t size) {
    /** TODO: Only write bytes to known-good modules. */
    for (uint8_t mram = 1; mram <= 3; mram++) {
        write_bytes(mram, address, data, size);
    }
}

#define PAGE_SIZE 256

void mram_read_bytes(uint32_t address, uint8_t *data, uint32_t size) {
    /** TODO: Only read bytes from known-good modules (no majority vote). */

    if (size % PAGE_SIZE != 0) {
        mram_recoverable_fatal();
    }
    uint32_t npages = size / PAGE_SIZE;

    uint8_t read_data[3][PAGE_SIZE];
    for (uint32_t page = 0; page < npages; page++) {
        for (uint8_t mram_idx = 1; mram_idx <= 3; mram_idx++) {
            read_bytes(mram_idx, address + PAGE_SIZE * page, read_data[mram_idx - 1], PAGE_SIZE);
        }

        bool any_error = false;
        for (uint16_t read_idx = 0; read_idx < PAGE_SIZE; read_idx++) {
            data[read_idx + PAGE_SIZE * page] = (read_data[0][read_idx] & read_data[1][read_idx]) |
                (read_data[2][read_idx] & read_data[1][read_idx]) |
                (read_data[0][read_idx] & read_data[2][read_idx]);

            if (read_data[0][read_idx] != read_data[1][read_idx] || read_data[0][read_idx] != read_data[2][read_idx]) {
                any_error = true;
            }
        }

        if (any_error) {
            mram_write_bytes(address + PAGE_SIZE * page, data + PAGE_SIZE * page, PAGE_SIZE);
        }
    }
}

// ---------------------- Test ----------------------

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
            mram_recoverable_fatal();
        }
    }
}

// ---------------------- Main ----------------------

/**
 * @brief Initializes MRAM modules.
 * @return An `mram_status_t` with the status of the three MRAM modules.
 */
mram_status_t mram_init(void) {
    atmel_start_init();
    spi_m_sync_enable(&SPI_MRAM);

    gpio_set_pin_level(MRAM1_CS, true);
    gpio_set_pin_level(MRAM2_CS, true);
    gpio_set_pin_level(MRAM3_CS, true);

    delay_ms(50);

    mram_status_t status = 0;

    for (uint8_t mram = 1; mram <= 3; mram++) {
        mram_init_status_t init_status = MRAM_INIT_OK;
        init_status |= check_device_id(mram);
        init_status |= disable_block_protection(mram);
        init_status |= set_persistent_mode(mram);

        if (init_status == MRAM_INIT_OK) {
            set_mram_state(&status, mram, MRAM_STATE_OK);
        } else {
            set_mram_state(&status, mram, MRAM_STATE_FAILED);
            mram_init_report_err(mram, init_status);
        }
    }

    return status;
}

// ---------------------- CRC-32 ----------------------

uint32_t crc32_table[256];

uint32_t crc32(const uint8_t *block, uint32_t size) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            uint32_t xor_val;
            if (crc & 1) {
                xor_val = 0xEDB88320;
            } else xor_val = 0;
            crc = (crc >> 1) ^ xor_val;
        }
        crc32_table[i] = crc;
    }

    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < size; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ block[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}