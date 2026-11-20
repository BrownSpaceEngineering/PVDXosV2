/**
 * src/checks/device_checks.h
 *
 * header file for checking the state of hardware device for PVDX
 *
 * Created: 20251102 SUN
 * Authors: Zach Mahan
 */

#ifndef CHECKS_DEVICE_CHECKS
#define CHECKS_DEVICE_CHECKS

#include <stdbool.h>
#include <stdint.h>

#include "globals.h"

/*
 * ADDING A NEW DEVICE CHECK / PLEASE READ:
 * - Add a new `check_some_device` function protype at the top of the device_checks.c src file matching the signature bool fn(void)
 * - Fill in the definition at the bottom so that it returns true on success and otherwise false
 * - Add the function to the table of device check functions, you most likely be replacing a `NULL` entry
 * - Start each public operation of the device's driver with a `device_disabled()` gate that fills the outputs with safe
 *   placeholder values and returns SUCCESS, so code using a dead device keeps working without knowing
 * - Call `device_recheck_if_due()` from the loop of the task that owns the device
 * - Run a debug session to make sure it's working as expected
 */

// How often a task re-runs the check of a disabled device it owns
#ifndef DEVICE_RECHECK_INTERVAL_MS
#define DEVICE_RECHECK_INTERVAL_MS (5u * 60u * 1000u)
#endif

/**
 * \brief to be run once on startup to check the health of each device
 * \return true when at least device has failed, else false
 */
bool check_all_devices_on_startup(void);

/**
 * \brief function to check devices by indexing into the `device_states` array through a device_ide_e
 */
bool check_device(device_id_t device_id);

/**
 * \brief simply unchecks a device so that the next check will re-run the device's check function
 */
void uncheck_device(device_id_t device_id);

/**
 * \brief useful if we want to ensure the next check of the device will re-run the device's check function
 * \return initial state (true/false) of checked, potentially useful for extra information on how a device might be misbehaving
 */
bool check_and_uncheck_device(device_id_t device_id);

/**
 * \brief true when the device's last check failed: its driver calls are then no-ops that return placeholder values.
 * A device whose check hasn't run yet, or is running now, isn't disabled.
 */
bool device_disabled(device_id_t device_id);

/**
 * \brief re-runs the check of a disabled device once DEVICE_RECHECK_INTERVAL_MS has passed since its last check, so a
 * device that comes back is enabled again. Call it from the loop of the task that owns the device.
 */
void device_recheck_if_due(device_id_t device_id);

/**
 * gets the string corresponding to the name of a device, primarily for debugging purposes
 */
const char *device_name_of(device_id_t device_id);

#endif
