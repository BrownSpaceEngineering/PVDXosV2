#ifndef MRAM_H
#define MRAM_H

// The MRAM golden-copy layout lives in image_format.h

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

mram_status_t mram_init(void);
// Reads one chip (1-3). Returns false if any SPI transfer fails.
bool read_bytes(uint8_t mram, uint32_t address, uint8_t *data, uint32_t size);
// Reads from the first chip that mram_init() found working; any size works. Returns false if there is no such chip
// or its read fails.
bool mram_read_bytes(uint32_t address, uint8_t *data, uint32_t size);
// Writes all three chips. Returns false if any chip's write fails.
bool mram_write_bytes(uint32_t address, const uint8_t *data, uint32_t size);
uint32_t crc32(const uint8_t *block, uint32_t size);

#endif