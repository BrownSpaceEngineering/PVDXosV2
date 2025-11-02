#include "checks/device_checks.h"

#include <assert.h>
#include <stdint.h>

#include "globals.h"
#include "logging.h"

device_check_state_t device_states[NUM_DEVICES] = {0};

void check_devices(device_id_e device_id) {
    if (device_id > NUM_DEVICES) {
        fatal("[ERROR] Invalid device_id_e passed to `check_devices`");
    }
    // TODO, finish
}
