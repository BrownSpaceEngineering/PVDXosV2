/**
 * src/checks/device_checks.c
 *
 * header file for checking the state of hardware device for PVDX
 *
 * Created: 20251102 SUN
 * Updated: 20251104 TUE
 * Authors: Zach Mahan
 */

#include "checks/device_checks.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "globals.h"
#include "logging.h"

// ~~~ for safety, these structures should be local to this source file ~~~
typedef struct device_check_state {
    bool checked;
    bool valid;
} device_check_state_t;

static device_check_state_t device_states[NUM_DEVICES] = {[0 ... NUM_DEVICES - 1] = {.checked = false, .valid = false}};

// TODO define, also use this or a switch statement?
static bool (*device_check_functions[NUM_DEVICES])(void) = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};

// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

bool check_all_devices_on_startup(void) {
    bool at_least_one_failed = false;

    for (device_id_e device_id = 0; device_id < NUM_DEVICES; device_id++) {
        at_least_one_failed = at_least_one_failed || !check_device(device_id);
    }

    return at_least_one_failed;
}

bool check_device(device_id_e device_id) {
    // guard against invalid device_id given
    if (device_id >= NUM_DEVICES) {
        fatal("[ERROR] Invalid device_id_e passed to `check_devices`");
        return false;
    }
    // guard against calling a null fn
    if (device_check_functions[device_id] == NULL) {
        fatal("[ERROR] check function for device_id: %p not defined", device_check_functions[device_id]);
        return false;
    }
    // only run the check function if "checked" is false for a device
    if (!device_states[device_id].checked) {
        device_states[device_id].valid = device_check_functions[device_id]();
        device_states[device_id].checked = true;
    }
    return device_states[device_id].valid;
}

void uncheck_device(device_id_e device_id) {
    device_states[device_id].checked = false;
}

bool check_and_uncheck_device(device_id_e device_id) {
    bool original_checked = device_states[device_id].checked;
    device_states[device_id].checked = false;
    return original_checked;
}
