#ifndef HARDWARE_WATCHDOG_UTILS_H
#define HARDWARE_WATCHDOG_UTILS_H

#include "atmel_start.h"

// This file defines slightly modified copies of functions from (ASF/hri/hri_wdt_d51.h) and (ASF/hal/include/hal_wdt.h). The key difference
// is that the functions are modified to take in a pointer to a volatile Wdt struct, rather than a pointer to a wdt_descriptor struct or an
// arbitrary hardware instance void*. Additionally, the functions have been renamed to be more human-readable.

static inline void watchdog_wait_for_register_sync(volatile Wdt *const p_watchdog, hri_wdt_syncbusy_reg_t reg) {
    while (p_watchdog->SYNCBUSY.reg & reg) {
        // Loop until the register is synchronized
    }
}

static inline bool watchdog_get_always_on_bit(volatile Wdt *const p_watchdog) {
    uint8_t tmp;
    watchdog_wait_for_register_sync(p_watchdog, WDT_SYNCBUSY_ENABLE | WDT_SYNCBUSY_WEN | WDT_SYNCBUSY_ALWAYSON);
    tmp = p_watchdog->CTRLA.reg;
    tmp = (tmp & WDT_CTRLA_ALWAYSON) >> WDT_CTRLA_ALWAYSON_Pos;
    return (bool)tmp;
}

static inline void watchdog_set_early_warning_offset(volatile Wdt *const p_watchdog, hri_wdt_ewctrl_reg_t mask) {
    WDT_CRITICAL_SECTION_ENTER();
    p_watchdog->EWCTRL.reg |= WDT_EWCTRL_EWOFFSET(mask);
    WDT_CRITICAL_SECTION_LEAVE();
}

static inline void watchdog_enable_early_warning(volatile Wdt *const p_watchdog) {
    p_watchdog->INTENSET.reg = WDT_INTENSET_EW;
}

static inline void watchdog_set_period(volatile Wdt *const p_watchdog, hri_wdt_config_reg_t data) {
    uint8_t tmp;
    WDT_CRITICAL_SECTION_ENTER();
    tmp = p_watchdog->CONFIG.reg;
    tmp &= ~WDT_CONFIG_PER_Msk;
    tmp |= WDT_CONFIG_PER(data);
    p_watchdog->CONFIG.reg = tmp;
    WDT_CRITICAL_SECTION_LEAVE();
}

static inline bool watchdog_get_early_warning_bit(volatile Wdt *const p_watchdog) {
    return (p_watchdog->INTFLAG.reg & WDT_INTFLAG_EW) >> WDT_INTFLAG_EW_Pos;
}

static inline void watchdog_clear_early_warning_bit(volatile Wdt *const p_watchdog) {
    p_watchdog->INTFLAG.reg = WDT_INTFLAG_EW;
}

static inline void watchdog_set_clear_register(volatile Wdt *const p_watchdog, hri_wdt_clear_reg_t data) {
    WDT_CRITICAL_SECTION_ENTER();
    p_watchdog->CLEAR.reg = data;
    watchdog_wait_for_register_sync(p_watchdog, WDT_SYNCBUSY_CLEAR);
    WDT_CRITICAL_SECTION_LEAVE();
}

static inline int32_t watchdog_enable(volatile Wdt *const p_watchdog) {
    WDT_CRITICAL_SECTION_ENTER();
    p_watchdog->CTRLA.reg |= WDT_CTRLA_ENABLE;
    watchdog_wait_for_register_sync(p_watchdog, WDT_SYNCBUSY_ENABLE | WDT_SYNCBUSY_WEN | WDT_SYNCBUSY_ALWAYSON);
    WDT_CRITICAL_SECTION_LEAVE();

    return ERR_NONE;
}

static inline int32_t watchdog_disable(volatile Wdt *const p_watchdog) {
    if (watchdog_get_always_on_bit(p_watchdog)) {
        return ERR_DENIED;
    } else {
        WDT_CRITICAL_SECTION_ENTER();
        p_watchdog->CTRLA.reg &= ~WDT_CTRLA_ENABLE;
        watchdog_wait_for_register_sync(p_watchdog, WDT_SYNCBUSY_ENABLE | WDT_SYNCBUSY_WEN | WDT_SYNCBUSY_ALWAYSON);
        WDT_CRITICAL_SECTION_LEAVE();
    }

    return ERR_NONE;
}

static inline int32_t watchdog_feed(volatile Wdt *const p_watchdog) {
    watchdog_set_clear_register(p_watchdog, WDT_CLEAR_CLEAR_KEY);
    return ERR_NONE;
}

#endif // HARDWARE_WATCHDOG_UTILS_H