/**
 * watchdog_driver.c
 *
 * Driver for the hardware watchdog timer on an Atmel SAMD51 microcontroller
 *
 * Created: January 28, 2024
 * Authors: Oren Kohavi, Tanish Makadia, Siddharta Laloux
 */

#include "watchdog_driver.h"

// This file defines slightly modified copies of functions from (ASF/hri/hri_wdt_d51.h) and (ASF/hal/include/hal_wdt.h). 
// The key difference is that the functions are modified to take in a pointer to a volatile Wdt struct, rather than a 
// pointer to a wdt_descriptor struct or an arbitrary hardware instance void*. Additionally, the functions have been 
// renamed to be more human-readable.

// Reference to the hardware watchdog timer on the SAMD51 microcontroller
static volatile Wdt *const p_watchdog = WDT;

void watchdog_setup(void) {
    // Choose the period of the hardware watchdog timer
    uint8_t watchdog_period = WDT_CONFIG_PER_CYC16384;

    // Disable the watchdog before configuring
    watchdog_disable();

    // Configure the watchdog
    uint8_t watchdog_earlywarning_period =
        watchdog_period - 1; // Each increment of 1 doubles the period (see ASF/samd51a/include/component/wdt.h)
    watchdog_set_early_warning_offset(watchdog_earlywarning_period); // Early warning will trigger halfway through the watchdog period
    watchdog_enable_early_warning();                       // Enable early warning interrupt
    watchdog_set_period(watchdog_period);                // Set the watchdog period
    watchdog_wait_for_register_sync(WDT_SYNCBUSY_ENABLE | WDT_SYNCBUSY_WEN); // Wait for register synchronization

    // Enable the watchdog
    watchdog_enable();

    // Configure the watchdog early warning interrupt
    NVIC_SetPriority(WDT_IRQn, 3);                      // Set the interrupt priority
    NVIC_EnableIRQ(WDT_IRQn);                           // Enable the WDT_IRQn interrupt
    NVIC_SetVector(WDT_IRQn, (uint32_t)(&WDT_Handler)); // When the WDT_IRQn interrupt is triggered, call the WDT_Handler function
}

void watchdog_wait_for_register_sync(hri_wdt_syncbusy_reg_t reg) {
    while (p_watchdog->SYNCBUSY.reg & reg) {
        // Loop until the register is synchronized
    }
}

bool watchdog_get_always_on_bit(void) {
    uint8_t tmp;
    watchdog_wait_for_register_sync(WDT_SYNCBUSY_ENABLE | WDT_SYNCBUSY_WEN | WDT_SYNCBUSY_ALWAYSON);
    tmp = p_watchdog->CTRLA.reg;
    tmp = (tmp & WDT_CTRLA_ALWAYSON) >> WDT_CTRLA_ALWAYSON_Pos;
    return (bool)tmp;
}

void watchdog_set_early_warning_offset(hri_wdt_ewctrl_reg_t mask) {
    WDT_CRITICAL_SECTION_ENTER();
    p_watchdog->EWCTRL.reg |= WDT_EWCTRL_EWOFFSET(mask);
    WDT_CRITICAL_SECTION_LEAVE();
}

void watchdog_enable_early_warning(void) {
    p_watchdog->INTENSET.reg = WDT_INTENSET_EW;
}

void watchdog_set_period(hri_wdt_config_reg_t data) {
    uint8_t tmp;
    WDT_CRITICAL_SECTION_ENTER();
    tmp = p_watchdog->CONFIG.reg;
    tmp &= ~WDT_CONFIG_PER_Msk;
    tmp |= WDT_CONFIG_PER(data);
    p_watchdog->CONFIG.reg = tmp;
    WDT_CRITICAL_SECTION_LEAVE();
}

bool watchdog_get_early_warning_bit(void) {
    return (p_watchdog->INTFLAG.reg & WDT_INTFLAG_EW) >> WDT_INTFLAG_EW_Pos;
}

void watchdog_clear_early_warning_bit(void) {
    p_watchdog->INTFLAG.reg = WDT_INTFLAG_EW;
}

void watchdog_set_clear_register(hri_wdt_clear_reg_t data) {
    WDT_CRITICAL_SECTION_ENTER();
    p_watchdog->CLEAR.reg = data;
    watchdog_wait_for_register_sync(WDT_SYNCBUSY_CLEAR);
    WDT_CRITICAL_SECTION_LEAVE();
}

int32_t watchdog_enable(void) {
    WDT_CRITICAL_SECTION_ENTER();
    p_watchdog->CTRLA.reg |= WDT_CTRLA_ENABLE;
    watchdog_wait_for_register_sync(WDT_SYNCBUSY_ENABLE | WDT_SYNCBUSY_WEN | WDT_SYNCBUSY_ALWAYSON);
    WDT_CRITICAL_SECTION_LEAVE();

    return ERR_NONE;
}

int32_t watchdog_disable(void) {
    if (watchdog_get_always_on_bit()) {
        return ERR_DENIED;
    } else {
        WDT_CRITICAL_SECTION_ENTER();
        p_watchdog->CTRLA.reg &= ~WDT_CTRLA_ENABLE;
        watchdog_wait_for_register_sync(WDT_SYNCBUSY_ENABLE | WDT_SYNCBUSY_WEN | WDT_SYNCBUSY_ALWAYSON);
        WDT_CRITICAL_SECTION_LEAVE();
    }

    return ERR_NONE;
}

/**
 * \fn watchdog_pet
 * 
 * \brief Sets the clear register of the hardware watchdog correctly to
 *        prevent a system-reset.
 * 
 * \param p_watchdog Pointer to a watchdog timer hardware instance
 */
void watchdog_pet(void) {
    watchdog_set_clear_register(WDT_CLEAR_CLEAR_KEY);
}

/**
 * \fn watchdog_kick
 * 
 * \brief Intentionally sets the hardware watchdog's clear register to an incorrect
 *        value to cause a system-reset.
 *
 * \param p_watchdog Pointer to a watchdog timer hardware instance
 *
 * \warning This function should never return because the system should reset
 */
void watchdog_kick(void) {
    watchdog_set_clear_register(0x12); 
}
