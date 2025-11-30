/**
 * src/checks/device_checks.h
 *
 * header file for checking the state of hardware device for PVDX
 *
 * Created: 20251102 SUN
 * Updated: 20251104 TUE
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
 * - Run a debug session to make sure it's working as expected
 */

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
 * gets the string corresponding to the name of a device, primarily for debugging purposes
 */
const char *device_name_of(device_id_t device_id);

#endif
