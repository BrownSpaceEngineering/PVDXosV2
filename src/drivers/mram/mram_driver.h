#ifndef MRAM_H
#define MRAM_H

// Shared by the bootloader (stage 2) and PVDXos. MRAM 0x000000-0x07EFFF holds the bootloader's golden copy of flash
// (layout in bootloader/src/image_format.h); PVDXos must not write there.

// One 256-byte page each, past the golden copy and the MRAM hardware test's scratch area (0x800F0)
#define MRAM_ADCS_TLE_ADDRESS (0x00090000) // Last committed TLE
#define MRAM_ADCS_UTC_ADDRESS (0x00090100) // Last saved UTC

#define MRAM_PEROVSKITE_BASE_ADDRESS (0x00090200) // Address of perovskite sweeps in MRAM, right after the ADCS records
#define MRAM_PEROVSKITE_SIZE (0x4800)             // Size of perovskite sweeps in MRAM

#include "atmel_start.h"
#include "hal_delay.h"
#include "hal_gpio.h"

typedef uint8_t mram_status_t;

#define MRAM_STATE_OFF (0x00)      // MRAM module powered off.
#define MRAM_STATE_OK (0x01)       // MRAM module functioning as expected.
#define MRAM_STATE_DEGRADED (0x02) // MRAM module appears mostly functional but may not be 100% reliable.
#define MRAM_STATE_FAILED (0x03)   // MRAM module not functioning.

static inline void set_mram_state(mram_status_t *status, uint8_t mram, uint8_t state) {
    uint8_t shift = (mram - 1) * 2;
    uint8_t mask = 0x03 << shift;
    *status = (*status & ~mask) | ((state & 0x03) << shift);
}

static inline uint8_t get_mram_state(mram_status_t status, uint8_t mram) {
    uint8_t shift = (mram - 1) * 2;
    return (status >> shift) & 0x03;
}

// Blocks for `ms` milliseconds. Each build provides its own: bootloader/src/mram_delay.c and
// src/drivers/mram/mram_delay.c (safe before and after the FreeRTOS scheduler starts).
void mram_delay_ms(uint16_t ms);

mram_status_t mram_init(void);
// Reads from the first chip that mram_init() found working; any size works. Returns false if there is no such chip
// or its read fails.
bool mram_read_bytes(uint32_t address, uint8_t *data, uint32_t size);
// Writes all three chips. Returns false if any chip's write fails.
bool mram_write_bytes(uint32_t address, const uint8_t *data, uint32_t size);
uint32_t crc32(const uint8_t *block, uint32_t size);

#endif