#ifndef HARDWARE_WATCHDOG_UTILS_H
#define HARDWARE_WATCHDOG_UTILS_H

#include "atmel_start.h"

// This file defines copies of functions from (ASF/hri/hri_wdt_d51.h) and (ASF/hal/include/hal_wdt.h) with support for volatile inputs

static inline void watchdog_wait_for_register_sync(volatile Wdt *const watchdog_p, hri_wdt_syncbusy_reg_t reg)
{
	while (watchdog_p->SYNCBUSY.reg & reg) {
	};
}

static inline bool watchdog_get_always_on_bit(volatile Wdt *const watchdog_p)
{
	uint8_t tmp;
	watchdog_wait_for_register_sync(watchdog_p, WDT_SYNCBUSY_ENABLE | WDT_SYNCBUSY_WEN | WDT_SYNCBUSY_ALWAYSON);
	tmp = watchdog_p->CTRLA.reg;
	tmp = (tmp & WDT_CTRLA_ALWAYSON) >> WDT_CTRLA_ALWAYSON_Pos;
	return (bool)tmp;
}

static inline void watchdog_set_early_warning_offset(volatile Wdt *const watchdog_p, hri_wdt_ewctrl_reg_t mask)
{
	WDT_CRITICAL_SECTION_ENTER();
	watchdog_p->EWCTRL.reg |= WDT_EWCTRL_EWOFFSET(mask);
	WDT_CRITICAL_SECTION_LEAVE();
}

static inline void watchdog_enable_early_warning(volatile Wdt *const watchdog_p)
{
	watchdog_p->INTENSET.reg = WDT_INTENSET_EW;
}

static inline void watchdog_set_period(volatile Wdt *const watchdog_p, hri_wdt_config_reg_t data)
{
	uint8_t tmp;
	WDT_CRITICAL_SECTION_ENTER();
	tmp = watchdog_p->CONFIG.reg;
	tmp &= ~WDT_CONFIG_PER_Msk;
	tmp |= WDT_CONFIG_PER(data);
	watchdog_p->CONFIG.reg = tmp;
	WDT_CRITICAL_SECTION_LEAVE();
}

static inline bool watchdog_get_early_warning_bit(volatile Wdt *const watchdog_p)
{
	return (watchdog_p->INTFLAG.reg & WDT_INTFLAG_EW) >> WDT_INTFLAG_EW_Pos;
}

static inline void watchdog_clear_early_warning_bit(volatile Wdt *const watchdog_p)
{
	watchdog_p->INTFLAG.reg = WDT_INTFLAG_EW;
}

static inline void watchdog_set_clear_register(volatile Wdt *const watchdog_p, hri_wdt_clear_reg_t data)
{
	WDT_CRITICAL_SECTION_ENTER();
	watchdog_p->CLEAR.reg = data;
	watchdog_wait_for_register_sync(watchdog_p, WDT_SYNCBUSY_CLEAR);
	WDT_CRITICAL_SECTION_LEAVE();
}

static inline int32_t watchdog_enable(volatile Wdt *const watchdog_p)
{
	WDT_CRITICAL_SECTION_ENTER();
	watchdog_p->CTRLA.reg |= WDT_CTRLA_ENABLE;
	watchdog_wait_for_register_sync(watchdog_p, WDT_SYNCBUSY_ENABLE | WDT_SYNCBUSY_WEN | WDT_SYNCBUSY_ALWAYSON);
	WDT_CRITICAL_SECTION_LEAVE();

	return ERR_NONE;
}

static inline int32_t watchdog_disable(volatile Wdt *const watchdog_p)
{
	if (watchdog_get_always_on_bit(watchdog_p)) {
		return ERR_DENIED;
	} else {
		WDT_CRITICAL_SECTION_ENTER();
	    watchdog_p->CTRLA.reg &= ~WDT_CTRLA_ENABLE;
	    watchdog_wait_for_register_sync(watchdog_p, WDT_SYNCBUSY_ENABLE | WDT_SYNCBUSY_WEN | WDT_SYNCBUSY_ALWAYSON);
	    WDT_CRITICAL_SECTION_LEAVE();
	}

	return ERR_NONE;
}

static inline int32_t watchdog_feed(volatile Wdt *const watchdog_p)
{
    watchdog_set_clear_register(watchdog_p, WDT_CLEAR_CLEAR_KEY);
	return ERR_NONE;
}

#endif // HARDWARE_WATCHDOG_UTILS_H