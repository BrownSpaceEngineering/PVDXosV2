/**
 * src/checks/device_checks.c
 *
 * header file for checking the state of hardware device for PVDX
 *
 * Created: 20251102 SUN
 * Authors: Zach Mahan
 */

#include "checks/device_checks.h"

#include <stddef.h>
#include <stdint.h>

#include "display_driver.h"
#include "globals.h"
#include "logging.h"
#include "magnetometer_driver.h"

// forward declarations of check functions
bool check_magnetometer(void);
bool check_display(void);
// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

// ~~~ for safety, these structures should be local to this source file ~~~
typedef struct device_check_state {
    bool checked;
    bool valid;
} device_check_state_t;

static device_check_state_t device_states[NUM_DEVICES] = {[0 ... NUM_DEVICES - 1] = {.checked = false, .valid = false}};

static bool (*device_check_functions[NUM_DEVICES])(void) = {
    //    func ptr      |  device_id_t
    // -------------------------------
    &check_magnetometer, // MAGNETOMETER_ID
    NULL,                // PHOTODIODE_ID
    NULL,                // GYROSCOPE_ID
    NULL,                // MRAM_ID
    NULL,                // MAGNETORQUERS_ID
    NULL,                // SBAND_ID
    NULL,                // UHF_ID
    NULL,                // EPS_ID
    &check_display,      // DISPLAY_ID
    NULL,                // CAMERA_ID
};

// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

bool check_all_devices_on_startup(void) {
    bool at_least_one_failed = false;

    info("CHECKING ALL DEVICES...\n");
    info("1 = OKAY / 0 = NOT OKAY...\n");
    for (device_id_t device_id = 0; device_id < NUM_DEVICES; device_id++) {
        bool device_okay = check_device(device_id);
        at_least_one_failed = at_least_one_failed || !device_okay;
        info("%-20s: %d\n", device_name_of(device_id), device_okay);
    }

    return at_least_one_failed;
}

bool check_device(device_id_t device_id) {
    // guard against invalid device_id given
    if (device_id >= NUM_DEVICES) {
        warning("Invalid device_id_t passed to `check_devices`\n");
        return false;
    }
    // guard against calling a null fn
    if (device_check_functions[device_id] == NULL) {
        warning("Check function for device_id %s is NULL\n", device_name_of(device_id));
        return false;
    }
    // only run the check function if "checked" is false for a device
    if (!device_states[device_id].checked) {
        device_states[device_id].valid = device_check_functions[device_id]();
        device_states[device_id].checked = true;
    }
    return device_states[device_id].valid;
}

void uncheck_device(device_id_t device_id) {
    device_states[device_id].checked = false;
}

bool check_and_uncheck_device(device_id_t device_id) {
    bool original_checked = device_states[device_id].checked;
    device_states[device_id].checked = false;
    return original_checked;
}

const char *device_name_of(device_id_t device_id) {
    static const char *names[NUM_DEVICES] = {
        "MAGNETOMETER_ID", "PHOTODIODE_ID", "GYROSCOPE_ID", "MRAM_ID",    "MAGNETORQUERS_ID",
        "SBAND_ID",        "UHF_ID",        "EPS_ID",       "DISPLAY_ID", "CAMERA_ID",
    };
    return names[device_id];
}

// ------------------- check function definitions ----------------------
// - these should wrap device drivers/hardware interaction functions

bool check_magnetometer(void) {
    return init_rm3100() == SUCCESS;
}

bool check_display(void) {
    return init_display_hardware() == SUCCESS;
}
