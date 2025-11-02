#ifndef CHECKS_DEVICE_CHECKS
#define CHECKS_DEVICE_CHECKS

#include <stdbool.h>
#include <stdint.h>

#include "globals.h"
// TODO put exact number

typedef struct device_check_state {
    bool checked;
    bool valid;
} device_check_state_t;

extern device_check_state_t device_states[NUM_DEVICES];

/**
 * \brief function to check devices by indexing into the `device_states` array through a device_ide_e
 */
void check_device(device_id_e device_id);

#endif
