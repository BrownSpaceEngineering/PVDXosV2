#ifndef MRAM_H
#define MRAM_H

#include "image_format.h"

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

/** @brief Current module states, including modules marked failed by reads/writes since `mram_init`. */
mram_status_t mram_get_status(void);

/**
 * @brief Reads majority-vote data from MRAM modules.
 * @param address Address to read from.
 * @param data Pointer to output.
 * @param size Size of data to be read, in bytes.
 * @return `false` if read succeeded; `true` otherwise.
 * @warning `size` must be divisible by 256.
 */
bool mram_read_bytes(uint32_t address, uint8_t *data, uint32_t size);

/**
 * @brief Writes data to all healthy MRAM modules.
 * @param address Destination address (any alignment).
 * @param data Data to be written.
 * @param size Size of data to be written, in bytes (any length).
 * @return `false` if every page reached at least one module; `true` otherwise.
 * @note Data is split at 256-byte device page boundaries; each page is a separate WRITE transaction and is written
 *       to every healthy module before the next page. A module whose write fails is marked failed and skipped for
 *       the remaining pages. On failure, earlier pages may already have been written.
 */
bool mram_write_bytes(uint32_t address, const uint8_t *data, uint32_t size);

/**
 * @brief Called by `mram_write_bytes` after each page, for progress logging.
 * @param address First MRAM address of the page chunk.
 * @param size Bytes in the chunk (at most 256).
 * @param written Bitmask of modules that accepted the chunk (bit 0 = MRAM1).
 * @param failed Bitmask of modules that failed it (and are now marked failed).
 * @note The driver's default is a weak no-op; stage 2 overrides it to log over RTT.
 */
void mram_write_page_hook(uint32_t address, uint32_t size, uint8_t written, uint8_t failed);
uint32_t crc32(const uint8_t *block, uint32_t size);

#endif