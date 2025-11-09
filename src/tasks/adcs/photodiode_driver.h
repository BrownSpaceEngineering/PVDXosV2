#ifndef PHOTODIODE_DRIVER_H
#define PHOTODIODE_DRIVER_H

#include "atmel_start.h"
#include "globals.h"
#include "logging.h"
#include "adcs_task.h"

// Function declarations
status_t init_photodiode_hardware(void);
status_t read_photodiodes(uint16_t *values);

#endif // PHOTODIODE_DRIVER_H
