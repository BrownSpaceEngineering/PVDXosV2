/**
 * watchdog_driver.c
 *
 * Hardware watchdog for stage 2 of the bootloader: started at boot, petted after every flash or MRAM block, and
 * petted once more before jumping to PVDXos, which leaves it running until PVDXos sets it up again itself.
 *
 * Created: November 3, 2026
 */

#include "watchdog_driver.h"

#include "hri_wdt_d51.h"

// Sets the watchdog period to 16384 cycles of its 1.024 kHz clock (about 16 s, the same as PVDXos) and enables it. The
// period can only be changed while the watchdog is off. If the always-on fuse is set, the watchdog is already running
// and its period can't be changed.
void watchdog_setup(void) {
    if (hri_wdt_get_CTRLA_ALWAYSON_bit(WDT)) {
        return;
    }
    hri_wdt_clear_CTRLA_ENABLE_bit(WDT);
    hri_wdt_write_CONFIG_PER_bf(WDT, WDT_CONFIG_PER_CYC16384_Val);
    hri_wdt_set_CTRLA_ENABLE_bit(WDT);
}

void watchdog_pet(void) {
    hri_wdt_write_CLEAR_reg(WDT, WDT_CLEAR_CLEAR_KEY_Val);
}
