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

/**
 * @brief Halts execution and waits for the watchdog to reboot the system.
 * @warning Should only be called when a reboot will definitely fix the fatal
 *          state. If the fatal state is unrecoverable, use
 *          `mram_complete_fatal` instead.
 * TODO: consider if having this at all is risky; we might want to remove this entirely
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

bool spi_write(const uint8_t *data, uint32_t len) {
    struct spi_xfer xfer = {
        .txbuf = (uint8_t*)data,
        .rxbuf = NULL,
        .size  = len
    };
    // The development boot mirror is 512 KiB: at 50 kHz a single transfer exceeds the watchdog period.
    // Chunk only the HAL calls, not the MRAM transaction; the caller keeps CS asserted throughout.
    while (len > 0u) {
        xfer.size = len > MRAM_SPI_CHUNK_SIZE ? MRAM_SPI_CHUNK_SIZE : len;
        if (spi_m_sync_transfer(&SPI_MRAM, &xfer) != (int32_t)xfer.size) {
            return false;
        }
        xfer.txbuf += xfer.size;
        len -= xfer.size;
        watchdog_pet();
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
        watchdog_pet();
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
        delay_ms(1);
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

bool read_bytes(uint8_t mram, uint32_t address, uint8_t *data, uint32_t size) {
    watchdog_pet();
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

    watchdog_pet();
    return success;
}

bool write_bytes(uint8_t mram, uint32_t address, const uint8_t *data, uint32_t size) {
    watchdog_pet();

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

    watchdog_pet();
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
    for (uint8_t mram = 1; mram <= 3; mram++) {
        bool succeeded = write_bytes(mram, address, data, size);
        if (!succeeded) {
            return true; // Write failed.
        }
    }
    return false; // Write succeeded.
}

#define PAGE_SIZE 256

bool mram_read_bytes(uint32_t address, uint8_t *data, uint32_t size) {
    /** TODO: Only read bytes from known-good modules (no majority vote). */

    if (size % PAGE_SIZE != 0) {
        return true; // Read failed.
    }
    uint32_t npages = size / PAGE_SIZE;

    uint8_t read_data[3][PAGE_SIZE];
    for (uint32_t page = 0; page < npages; page++) {
        for (uint8_t mram_idx = 1; mram_idx <= 3; mram_idx++) {
            if (!read_bytes(mram_idx, address + PAGE_SIZE * page, read_data[mram_idx - 1], PAGE_SIZE)) {
                // Never majority-vote or repair from incomplete read buffers.
                // The void API cannot report failure; a caller's checksum may still match stale RAM.
                return true; // Read failed.
            }
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
    return false; // Read succeeded.
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

    return status;
}

#if defined(UNITTEST) && defined(TEST_MRAM)
#include <string.h>

#include "logging.h"
#include "tests/test.h"

// Outside the bootloader's development flash mirror; deliberately cross 256-byte boundaries.
#define MRAM_HW_TEST_ADDRESS 0x000800F0u
#define MRAM_HW_TEST_SIZE 512u

static bool mram_test_compare(uint8_t mram, const uint8_t *actual, const uint8_t *expected) {
    for (unsigned int i = 0; i < MRAM_HW_TEST_SIZE; i++) {
        if (actual[i] != expected[i]) {
            warning("MRAM HW chip %u mismatch at %06lx: read=%02x expected=%02x\n", mram,
                    (unsigned long)(MRAM_HW_TEST_ADDRESS + i), actual[i], expected[i]);
            return false;
        }
    }
    return true;
}

void test_mram(void) {
    static uint8_t original[3][MRAM_HW_TEST_SIZE];
    static uint8_t expected[MRAM_HW_TEST_SIZE];
    static uint8_t actual[MRAM_HW_TEST_SIZE];
    bool saved[3] = {false};
    bool all_okay = true;

    spi_m_sync_enable(&SPI_MRAM);
    gpio_set_pin_level(MRAM1_CS, true);
    gpio_set_pin_level(MRAM2_CS, true);
    gpio_set_pin_level(MRAM3_CS, true);
    delay_ms(200);
    info("MRAM HW BEGIN: audit shared driver, 512 bytes/chip at %06lx\n", (unsigned long)MRAM_HW_TEST_ADDRESS);
#ifdef MRAM_FACTORY_INIT
    extern bool mram_factory_initialize_all(void);
    bool factory_okay = mram_factory_initialize_all();
    PVDX_ASSERT_MSG(factory_okay, "MRAM verified factory initialization\n");
    if (!factory_okay) {
        info("MRAM HW END: FAIL (factory initialization)\n");
        return;
    }
#endif

    for (uint8_t mram = 1; mram <= 3; mram++) {
        uint8_t cmd = CMD_RDID;
        uint8_t id[3] = {0};
        mram_select(mram);
        bool okay = spi_write(&cmd, 1) && spi_read(id, sizeof(id));
        mram_deselect(mram);
        okay = okay && id[0] == 0x6B && id[1] == 0xBB && id[2] == 0x14;
        info("MRAM HW chip %u ID=%02x %02x %02x: %s\n", mram, id[0], id[1], id[2], okay ? "PASS" : "FAIL");
        PVDX_ASSERT_MSG(okay, "MRAM hardware ID\n");
        if (!okay) {
            all_okay = false;
            continue;
        }

        uint8_t initial_status = read_status(mram);
        okay = disable_block_protection(mram) == MRAM_INIT_OK && set_persistent_mode(mram) == MRAM_INIT_OK;
        info("MRAM HW chip %u status=%02x NV8=%02x V8=%02x init=%s\n", mram, initial_status,
             read_nonvol_reg(mram, PMM_REG), read_vol_reg(mram, PMM_REG), okay ? "PASS" : "FAIL");
        PVDX_ASSERT_MSG(okay, "MRAM hardware initialization\n");
        saved[mram - 1] = okay && read_bytes(mram, MRAM_HW_TEST_ADDRESS, original[mram - 1], MRAM_HW_TEST_SIZE) &&
                          read_bytes(mram, MRAM_HW_TEST_ADDRESS, actual, MRAM_HW_TEST_SIZE) &&
                          mram_test_compare(mram, actual, original[mram - 1]);
        PVDX_ASSERT_MSG(saved[mram - 1], "MRAM stable original snapshot\n");
        if (!saved[mram - 1]) {
            all_okay = false;
            continue;
        }

        // Exercise the two BP bits omitted by the old 0x0C mask, then verify their removal.
        for (unsigned int bit = 0; bit < 2; bit++) {
            uint8_t protected_status = (initial_status & 0xA0u) | (bit == 0 ? 0x10u : 0x40u);
            bool protection_okay = write_status(mram, protected_status) &&
                                   (read_status(mram) & 0xFCu) == protected_status &&
                                   disable_block_protection(mram) == MRAM_INIT_OK &&
                                   (read_status(mram) & 0xFCu) == (initial_status & 0xA0u);
            info("MRAM HW chip %u BP=%02x set/clear/preserve: %s\n", mram,
                 bit == 0 ? 0x10 : 0x40, protection_okay ? "PASS" : "FAIL");
            PVDX_ASSERT_MSG(protection_okay, "MRAM hardware protection regression\n");
            okay = okay && protection_okay;
        }
        // Always try to leave the device writable, including after a failed protection test.
        okay = (disable_block_protection(mram) == MRAM_INIT_OK) && okay;

        for (unsigned int pattern = 0; pattern < 5 && okay; pattern++) {
            uint32_t random_state = 0xA53C9671u ^ (mram * 17u) ^ (pattern * 0x17171717u);
            for (unsigned int i = 0; i < MRAM_HW_TEST_SIZE; i++) {
                random_state ^= random_state << 13u;
                random_state ^= random_state >> 17u;
                random_state ^= random_state << 5u;
                expected[i] = pattern == 0 ? (uint8_t)((i * 37u) ^ (mram * 13u)) :
                              pattern == 1 ? 0x00u : pattern == 2 ? 0xFFu : (uint8_t)random_state;
            }
            bool pattern_okay = write_bytes(mram, MRAM_HW_TEST_ADDRESS, expected, MRAM_HW_TEST_SIZE) &&
                                read_bytes(mram, MRAM_HW_TEST_ADDRESS, actual, MRAM_HW_TEST_SIZE) &&
                                mram_test_compare(mram, actual, expected);
            info("MRAM HW chip %u pattern %u (512 bytes, cross-page): %s\n", mram, pattern, pattern_okay ? "PASS" : "FAIL");
            PVDX_ASSERT_MSG(pattern_okay, "MRAM hardware pattern readback\n");
            okay = okay && pattern_okay;
        }
        all_okay = all_okay && okay;
        delay_ms(200);
    }

    if (all_okay) {
        for (unsigned int i = 0; i < MRAM_HW_TEST_SIZE; i++) {
            expected[i] = (uint8_t)((i * 19u) ^ 0xA5u);
        }
        mram_write_bytes(MRAM_HW_TEST_ADDRESS, expected, MRAM_HW_TEST_SIZE);
        for (uint8_t mram = 1; mram <= 3; mram++) {
            bool replica_okay = read_bytes(mram, MRAM_HW_TEST_ADDRESS, actual, MRAM_HW_TEST_SIZE) &&
                                mram_test_compare(mram, actual, expected);
            PVDX_ASSERT_MSG(replica_okay, "MRAM hardware public write replica\n");
            all_okay = all_okay && replica_okay;
        }
        // Inject a different flipped bit in each replica: bitwise majority must recover the original.
        for (uint8_t mram = 1; mram <= 3; mram++) {
            uint8_t corrupted = expected[0] ^ (1u << (mram - 1));
            bool injected = write_bytes(mram, MRAM_HW_TEST_ADDRESS, &corrupted, 1);
            PVDX_ASSERT_MSG(injected, "MRAM hardware majority fault injection\n");
            all_okay = all_okay && injected;
        }
        memset(actual, 0, sizeof(actual));
        mram_read_bytes(MRAM_HW_TEST_ADDRESS, actual, MRAM_HW_TEST_SIZE);
        bool majority_okay = mram_test_compare(0, actual, expected);
        PVDX_ASSERT_MSG(majority_okay, "MRAM hardware majority read\n");
        all_okay = all_okay && majority_okay;
        for (uint8_t mram = 1; mram <= 3; mram++) {
            bool repaired = read_bytes(mram, MRAM_HW_TEST_ADDRESS, actual, MRAM_HW_TEST_SIZE) &&
                            mram_test_compare(mram, actual, expected);
            info("MRAM HW chip %u public write / majority repair: %s\n", mram, repaired ? "PASS" : "FAIL");
            PVDX_ASSERT_MSG(repaired, "MRAM hardware replica repair\n");
            all_okay = all_okay && repaired;
        }
    }

    for (uint8_t mram = 1; mram <= 3; mram++) {
        if (!saved[mram - 1]) {
            continue;
        }
        bool restored = disable_block_protection(mram) == MRAM_INIT_OK &&
                        write_bytes(mram, MRAM_HW_TEST_ADDRESS, original[mram - 1], MRAM_HW_TEST_SIZE) &&
                        read_bytes(mram, MRAM_HW_TEST_ADDRESS, actual, MRAM_HW_TEST_SIZE) &&
                        mram_test_compare(mram, actual, original[mram - 1]);
        for (unsigned int repeat = 0; restored && repeat < 2; repeat++) {
            restored = read_bytes(mram, MRAM_HW_TEST_ADDRESS, actual, MRAM_HW_TEST_SIZE) &&
                       mram_test_compare(mram, actual, original[mram - 1]);
        }
        info("MRAM HW chip %u original 512-byte restoration: %s\n", mram, restored ? "PASS" : "FAIL");
        PVDX_ASSERT_MSG(restored, "MRAM hardware restoration\n");
        all_okay = all_okay && restored;
        delay_ms(200);
    }
    info("MRAM HW END: %s\n", all_okay ? "PASS" : "FAIL");
}
#endif

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