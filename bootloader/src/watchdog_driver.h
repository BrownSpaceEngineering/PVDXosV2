#ifndef WATCHDOG_DRIVER_H
#define WATCHDOG_DRIVER_H

// Hardware watchdog for stage 2 of the bootloader. PVDXos sets the watchdog up again itself (init_watchdog).

#include "atmel_start.h"

void watchdog_setup(void);
void watchdog_pet(void);

#endif // WATCHDOG_DRIVER_H
