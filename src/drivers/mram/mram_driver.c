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
#define CMD_RDFSR   0x70    // Read Flag Status Register
#define CMD_CLFSR   0x50    // Clear Flag Status Register

#define MRAM_STATUS_WIP 0x01u
#define MRAM_STATUS_WEL 0x02u
#define MRAM_STATUS_BP_MASK 0x5Cu // BP[3:0] is SR[6,4:2], not a contiguous field.
#define MRAM_FLAG_READY 0x80u
#define MRAM_FLAG_ERRORS 0x3Au
#define MRAM_READY_TIMEOUT_MS 20u
#define MRAM_SPI_CHUNK_SIZE 512u

#define PMM_REG     8       // Persistent Memory Mode register

typedef enum {
    MRAM_INIT_OK = 0,                        // No issues
    MRAM_INIT_BAD_ID = 1u << 1,              // Device ID does not match expected value
    MRAM_INIT_FAIL_BLOCK_PROT = 1u << 2,     // Block protection not disabled
    MRAM_INIT_FAIL_PERSIST = 1u << 3,        // Persistence mode not enabled
} mram_init_status_t;

// ---------------------- SPI Helpers ----------------------

void mram_recoverable_fatal(void) { while (1); }

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

bool spi_write(const uint8_t *data, uint32_t len) {
    struct spi_xfer xfer = {
        .txbuf = (uint8_t*)data,
        .rxbuf = NULL,
        .size  = len
    };
    // Chunk only the HAL calls, not the MRAM transaction; the caller keeps CS asserted throughout.
    while (len > 0u) {
        xfer.size = len > MRAM_SPI_CHUNK_SIZE ? MRAM_SPI_CHUNK_SIZE : len;
        if (spi_m_sync_transfer(&SPI_MRAM, &xfer) != (int32_t)xfer.size) {
            return false;
        }
        xfer.txbuf += xfer.size;
        len -= xfer.size;
    }
    return true;
}

bool spi_read(uint8_t *data, uint32_t len) {
    struct spi_xfer xfer = {
        .txbuf = NULL,
        .rxbuf = data,
        .size  = len
    };
    while (len > 0u) {
        xfer.size = len > MRAM_SPI_CHUNK_SIZE ? MRAM_SPI_CHUNK_SIZE : len;
        if (spi_m_sync_transfer(&SPI_MRAM, &xfer) != (int32_t)xfer.size) {
            return false;
        }
        xfer.rxbuf += xfer.size;
        len -= xfer.size;
    }
    return true;
}

// ---------------------- MRAM Commands ----------------------

static bool mram_read_register(uint8_t mram, uint8_t cmd, uint8_t *value) {
    mram_select(mram);
    bool success = spi_write(&cmd, 1) && spi_read(value, 1);
    mram_deselect(mram);
    return success;
}

uint8_t read_status(uint8_t mram) {
    uint8_t status = 0xFF;
    mram_read_register(mram, CMD_RDSR, &status);
    return status;
}

static bool mram_wait_ready(uint8_t mram) {
    for (unsigned int elapsed = 0; elapsed < MRAM_READY_TIMEOUT_MS; elapsed++) {
        uint8_t status = 0;
        if (!mram_read_register(mram, CMD_RDSR, &status)) {
            return false;
        }
        if ((status & MRAM_STATUS_WIP) == 0u) {
            return true;
        }
        mram_delay_ms(1);
    }
    return false;
}

static bool mram_clear_flags(uint8_t mram) {
    uint8_t cmd = CMD_CLFSR;
    mram_select(mram);
    bool success = spi_write(&cmd, 1);
    mram_deselect(mram);
    return success;
}

static bool mram_operation_okay(uint8_t mram) {
    uint8_t flags = 0;
    return mram_wait_ready(mram) && mram_read_register(mram, CMD_RDFSR, &flags) &&
           (flags & (MRAM_FLAG_READY | MRAM_FLAG_ERRORS)) == MRAM_FLAG_READY;
}

bool write_enable(uint8_t mram) {
    uint8_t cmd = CMD_WREN;
    mram_select(mram);
    bool success = spi_write(&cmd, 1);
    mram_deselect(mram);
    uint8_t status = 0;
    return success && mram_read_register(mram, CMD_RDSR, &status) && (status & MRAM_STATUS_WEL) != 0u;
}

bool write_status(uint8_t mram, uint8_t value) {
    uint8_t buf[2] = {CMD_WRSR, value};
    if (!mram_wait_ready(mram) || !mram_clear_flags(mram) || !write_enable(mram)) {
        return false;
    }
    mram_select(mram);
    bool success = spi_write(buf, 2);
    mram_deselect(mram);
    return success && mram_operation_okay(mram);
}

static bool read_bytes(uint8_t mram, uint32_t address, uint8_t *data, uint32_t size) {
    if (!mram_wait_ready(mram)) {
        return false;
    }
    
    uint8_t cmd[4] = {
        CMD_READ,
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)(address)
    };
    mram_select(mram);
    bool success = spi_write(cmd, 4) && spi_read(data, size);
    mram_deselect(mram);

    return success;
}

static bool write_bytes(uint8_t mram, uint32_t address, const uint8_t *data, uint32_t size) {
    uint8_t hdr[4] = {
        CMD_WRITE,
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)(address)
    };
    if (!mram_wait_ready(mram) || !mram_clear_flags(mram) || !write_enable(mram)) {
        return false;
    }
    mram_select(mram);
    bool success = spi_write(hdr, 4) && spi_write(data, size);
    mram_deselect(mram);
    success = success && mram_operation_okay(mram);

    return success;
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
    bool success = spi_write(&cmd, 1) && spi_read(id, 3);
    mram_deselect(mram);

    if (!success || id[0] != 0x6B || id[1] != 0xBB || id[2] != 0x14) {
        // invalid device ID
        return MRAM_INIT_BAD_ID;
    } else {
        return MRAM_INIT_OK;
    }
}

mram_init_status_t disable_block_protection(uint8_t mram) {
    uint8_t status = 0;
    if (!mram_wait_ready(mram) || !mram_read_register(mram, CMD_RDSR, &status)) {
        return MRAM_INIT_FAIL_BLOCK_PROT;
    }
    if ((status & MRAM_STATUS_BP_MASK) == 0u) {
        return MRAM_INIT_OK;
    }

    // WRSR is nonvolatile: preserve SR[7] and TB, and write only when BP bits are set.
    uint8_t unprotected_status = status & 0xA0u;
    if (!write_status(mram, unprotected_status) || !mram_read_register(mram, CMD_RDSR, &status) ||
        (status & 0xFCu) != unprotected_status) {
        return MRAM_INIT_FAIL_BLOCK_PROT;
    }
    return MRAM_INIT_OK;
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

bool mram_write_bytes(uint32_t address, const uint8_t *data, uint32_t size) {
    /** TODO: Only write bytes to known-good modules. */
    bool all_okay = true;
    for (uint8_t mram = 1; mram <= 3; mram++) {
        all_okay = write_bytes(mram, address, data, size) && all_okay;
    }
    return all_okay;
}

bool mram_read_chip(uint8_t mram, uint32_t address, uint8_t *data, uint32_t size) {
    return mram >= 1 && mram <= 3 && read_bytes(mram, address, data, size);
}

bool mram_write_chip(uint8_t mram, uint32_t address, const uint8_t *data, uint32_t size) {
    return mram >= 1 && mram <= 3 && write_bytes(mram, address, data, size);
}

// States from the last mram_init(), so that reads can pick a chip that initialized correctly
static mram_status_t mram_states = 0;

// Reads from the first chip that initialized correctly. Any size and alignment works.
bool mram_read_bytes(uint32_t address, uint8_t *data, uint32_t size) {
    for (uint8_t mram = 1; mram <= 3; mram++) {
        if (get_mram_state(mram_states, mram) == MRAM_STATE_OK) {
            return read_bytes(mram, address, data, size);
        }
    }
    return false;
}

// ---------------------- Main ----------------------

mram_status_t mram_init(void) {
    spi_m_sync_enable(&SPI_MRAM);
    gpio_set_pin_level(MRAM1_CS, true);
    gpio_set_pin_level(MRAM2_CS, true);
    gpio_set_pin_level(MRAM3_CS, true);

    mram_delay_ms(50);

    mram_status_t status = 0;

    for (uint8_t mram = 1; mram <= 3; mram++) {
        mram_init_status_t init_status = MRAM_INIT_OK;
        init_status |= check_device_id(mram);
        if (init_status == MRAM_INIT_OK) {
            init_status |= disable_block_protection(mram);
        }
        if (init_status == MRAM_INIT_OK) {
            init_status |= set_persistent_mode(mram);
        }

        if (init_status == MRAM_INIT_OK) {
            set_mram_state(&status, mram, MRAM_STATE_OK);
        } else {
            set_mram_state(&status, mram, MRAM_STATE_FAILED);
            mram_init_report_err(mram, init_status);
        }
    }

    mram_states = status;
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