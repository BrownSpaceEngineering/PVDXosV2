#ifndef PVD_DAC_H
#define PVD_DAC_H

#include <stdbool.h>
#include <stdint.h>

#include "driver_init.h"
#include "globals.h"
#include "logging.h"

// PVD_CRNT is DAC VOUT0 on PA02 (DAC_0 channel 0 in Atmel START)
#define PVD_DAC DAC_0
#define PVD_DAC_CHANNEL 0U

#define PVD_DAC_MAX_CODE 4095U /* 12-bit DAC */
#define PVD_DAC_VREF_MV 3300U  /* Assumes DAC REFSEL = VDDANA */

// VDAC -> R1 (12k) / R2 (21k) divider -> TLV9001 follower (FOLLOW) -> 10 ohm shunt -> selected pixel. The
// follower regulates FOLLOW, so the pixel sits at the commanded voltage plus the shunt drop (see ina226.h).
// Full scale = 3300 mV * 21k / 33k = 2100 mV.
#define PVD_DIVIDER_TOP_OHMS 12000U    // R1
#define PVD_DIVIDER_BOTTOM_OHMS 21000U // R2
#define PVD_PIXEL_FULL_SCALE_MV (PVD_DAC_VREF_MV * PVD_DIVIDER_BOTTOM_OHMS / (PVD_DIVIDER_TOP_OHMS + PVD_DIVIDER_BOTTOM_OHMS))

status_t pvd_dac_init(void);
bool pvd_dac_is_ready(void);
status_t pvd_dac_set_code(uint16_t code);
status_t pvd_dac_set_mv(uint32_t dac_mv);
status_t pvd_dac_set_pixel_mv(uint32_t pixel_mv);

#endif // PVD_DAC_H
