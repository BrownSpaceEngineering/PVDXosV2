#ifndef PHOTODIODE_DRIVER_H
#define PHOTODIODE_DRIVER_H

#include "atmel_start.h"
#include "globals.h"
#include "logging.h"

// Photodiode system constants needed in type definitions
#define PHOTODIODE_COUNT 22   // Number of photodiodes (8 mux + 14 direct)

// Photodiode data structures
typedef struct {
    uint16_t raw_values[PHOTODIODE_COUNT];        // Raw ADC readings (up to 22)
    uint32_t timestamp;                               // Reading timestamp
    bool valid;                                       // Data validity flag
} photodiode_data_t;

// Function declarations
status_t init_photodiode_hardware(void);
status_t read_photodiodes(uint16_t *values);

status_t photodiode_read(photodiode_data_t *const data); 

#endif // PHOTODIODE_DRIVER_H
