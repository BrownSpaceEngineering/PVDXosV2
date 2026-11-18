#ifndef MRAM_H
#define MRAM_H

#include "atmel_start.h"
#include "hal_delay.h"
#include "hal_gpio.h"
#include "image_format.h"

/**
 * @brief A type representing the current status of all MRAM modules.
 *
 * @note Use `get_mram_state` and `set_mram_state` to interact with this data. Use the
 * predefined states `MRAM_STATE_OFF`, `MRAM_STATE_OK`, `MRAM_STATE_DEGRADED`, and `MRAM_STATE_FAILED`.
 */
typedef uint8_t mram_status_t;

#define MRAM_STATE_OFF (0x00) // Not initialized: the value before mram_init(). Nothing powers modules off.
#define MRAM_STATE_OK (0x01)  // MRAM module functioning as expected.
// Still read and written, but its data was wrong this boot: outvoted, or its own copy failed the caller's check.
// Reporting only (boot_record->mram_status_final); mram_init() resets every module each boot.
#define MRAM_STATE_DEGRADED (0x02)
#define MRAM_STATE_FAILED (0x03) // Init, a read or a write failed; skipped for the rest of the boot.

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

/** Prepare reads once after board initialization; does not write MRAM configuration. */
mram_status_t mram_init_for_reads(void);

/** @brief Current module states, including modules marked failed by reads/writes since `mram_init`. */
mram_status_t mram_get_status(void);

/**
 * @brief Counts the modules in `status` that are usable (OK or degraded).
 */
static inline uint8_t mram_healthy_count(mram_status_t status) {
    uint8_t count = 0;
    for (uint8_t mram = 1; mram <= 3; mram++) {
        uint8_t state = get_mram_state(status, mram);
        count += (state == MRAM_STATE_OK || state == MRAM_STATE_DEGRADED) ? 1u : 0u;
    }
    return count;
}

/**
 * @brief Reads majority-vote data from the healthy MRAM modules (bitwise vote per 256-byte page).
 * @param address Address to read from.
 * @param data Pointer to output.
 * @param size Size of data to be read, in bytes.
 * @param[out] replicas_differ Set to whether any healthy replica disagreed with the result (may be NULL).
 * @return `false` if read succeeded; `true` otherwise (no module readable, or two modules that disagree).
 * @note Never writes to MRAM. A vote is wrong if two replicas flipped the same bit, so the caller should check the
 *       data (e.g. by CRC) and only then scrub with `mram_write_bytes`; on a failed check, `mram_read_module` lets
 *       it try each copy on its own.
 * @warning `size` must be divisible by 256.
 */
bool mram_read_bytes(uint32_t address, uint8_t *data, uint32_t size, bool *replicas_differ);

/**
 * @brief Marks a healthy module (1-3) degraded: its data was found wrong. Leaves failed or degraded modules alone.
 */
void mram_mark_degraded(uint8_t mram);

/**
 * @brief Reads one module (1-3) without voting. A module whose read fails is marked failed.
 * @return `false` if read succeeded; `true` if the module is not healthy or the read failed.
 */
bool mram_read_module(uint8_t mram, uint32_t address, uint8_t *data, uint32_t size);

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

uint32_t crc32(const uint8_t *block, uint32_t size);

#endif