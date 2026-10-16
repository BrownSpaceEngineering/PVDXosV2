#ifndef WATCHDOG_DRIVER_H
#define WATCHDOG_DRIVER_H

#include "atmel_start.h"
#include "globals.h"

void watchdog_wait_for_register_sync(volatile Wdt *const p_watchdog, hri_wdt_syncbusy_reg_t reg);
bool watchdog_get_always_on_bit(volatile Wdt *const p_watchdog);
void watchdog_set_early_warning_offset(volatile Wdt *const p_watchdog, hri_wdt_ewctrl_reg_t mask);
void watchdog_enable_early_warning(volatile Wdt *const p_watchdog);
void watchdog_set_period(volatile Wdt *const p_watchdog, hri_wdt_config_reg_t data);
bool watchdog_get_early_warning_bit(volatile Wdt *const p_watchdog);
void watchdog_clear_early_warning_bit(volatile Wdt *const p_watchdog);
void watchdog_set_clear_register(volatile Wdt *const p_watchdog, hri_wdt_clear_reg_t data);
int32_t watchdog_enable(volatile Wdt *const p_watchdog);
int32_t watchdog_disable(volatile Wdt *const p_watchdog);
void watchdog_feed(volatile Wdt *const p_watchdog);
void watchdog_kick(volatile Wdt *const p_watchdog);

#endif // WATCHDOG_DRIVER_H