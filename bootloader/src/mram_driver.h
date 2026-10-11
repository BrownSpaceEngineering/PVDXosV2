#ifndef MRAM_H
#define MRAM_H

// Set this flag to commit the OS and bootloaders from flash into MRAM (for development)
// #define MRAM_OS_WRITE

// Set this flag to load the OS from MRAM on boot and reflash the bootloaders from MRAM
// #define MRAM_OS_READ

#define MRAM_OS_BASE_ADDRESS (0x00020000) // Address of OS in MRAM
#define MRAM_OS_SIZE (0x20000)            // Size of OS in MRAM

#define MRAM_FLASH_BASE_ADDRESS (0x00000000) // Address of complete flash copy in MRAM
#define MRAM_FLASH_SIZE (0x80000)            // Size of complete flash copy in MRAM

#include "atmel_start.h"
#include "hal_delay.h"
#include "hal_gpio.h"

/**
 * @brief A type representing the current status of all MRAM modules.
 *
 * @note Use `get_mram_state` and `set_mram_state` to interact with this data. Use the
 * predefined states `MRAM_STATE_OFF`, `MRAM_STATE_OK`, `MRAM_STATE_DEGRADED`, and `MRAM_STATE_FAILED`.
 */
typedef uint8_t mram_status_t;

#define MRAM_STATE_OFF (0x00)      // MRAM module powered off.
#define MRAM_STATE_OK (0x01)       // MRAM module functioning as expected.
#define MRAM_STATE_DEGRADED (0x02) // MRAM module appears mostly functional but may not be 100% reliable.
#define MRAM_STATE_FAILED (0x03)   // MRAM module not functioning.

/**
 * @brief Sets the state of the specified MRAM module to the specified status.
 *
 * @param[out] status Pointer to the `mram_status_t` object to modify.
 * @param mram Number (1 through 3) of the MRAM module to update status for.
 * @param state New MRAM state.
 */
static inline void set_mram_state(mram_status_t *status, uint8_t mram, uint8_t state) {
    uint8_t shift = (mram - 1) * 2;
    uint8_t mask = 0x03 << shift;
    *status = (*status & ~mask) | ((state & 0x03) << shift);
}

/**
 * @brief Retrieves the state of the specified MRAM module from the given MRAM status object.
 *
 * @param status MRAM status object to read from.
 * @param mram Number (1 through 3) of the MRAM module to read state for.
 * @returns State of the specified MRAM module, corresponding to the predefined states `MRAM_STATE_OFF`, `MRAM_STATE_OK`,
 * `MRAM_STATE_DEGRADED`, and `MRAM_STATE_FAILED`.
 */
static inline uint8_t get_mram_state(mram_status_t status, uint8_t mram) {
    uint8_t shift = (mram - 1) * 2;
    return (status >> shift) & 0x03;
}

mram_status_t mram_init(void);
void mram_read_bytes(uint32_t address, uint8_t *data, uint32_t size);
void mram_write_bytes(uint32_t address, const uint8_t *data, uint32_t size);
uint32_t crc32(const uint8_t *block, uint32_t size);

#endif