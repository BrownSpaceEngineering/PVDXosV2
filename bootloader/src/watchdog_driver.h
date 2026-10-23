#ifndef WATCHDOG_DRIVER_H
#define WATCHDOG_DRIVER_H

#include "atmel_start.h"
// #include "globals.h"

void watchdog_setup(void);
void watchdog_wait_for_register_sync(hri_wdt_syncbusy_reg_t reg);
bool watchdog_get_always_on_bit(void);
void watchdog_set_early_warning_offset(hri_wdt_ewctrl_reg_t mask);
void watchdog_enable_early_warning(void);
void watchdog_set_period(hri_wdt_config_reg_t data);
bool watchdog_get_early_warning_bit(void);
void watchdog_clear_early_warning_bit(void);
void watchdog_set_clear_register(hri_wdt_clear_reg_t data);
int32_t watchdog_enable(void);
int32_t watchdog_disable(void);
void watchdog_pet(void);
void watchdog_kick(void);

#endif // WATCHDOG_DRIVER_H