#ifndef PHOTODIODE_DRIVER_H
#define PHOTODIODE_DRIVER_H

#include "globals.h"
#include "logging.h"
#include "rtos_start.h"

// Photodiode system constants
#define PHOTODIODE_COUNT 22 // Number of photodiodes (8 mux + 14 direct)

#define PHOTODIODE_S0_PIN (Photodiode_MUX_S0 & 0x1Fu)
#define PHOTODIODE_MUX_MASK (0xFu << PHOTODIODE_S0_PIN)

// Photodiode data structures
typedef struct {
    uint16_t raw_values[PHOTODIODE_COUNT]; // Raw ADC readings (up to 22)
    uint32_t timestamp;                    // Reading timestamp
    bool valid;                            // Data validity flag
} photodiode_data_t;

// Function declarations
status_t init_photodiode_hardware(void);
status_t read_photodiodes(uint16_t *values);
status_t photodiode_read(photodiode_data_t *const data);

#endif // PHOTODIODE_DRIVER_H
