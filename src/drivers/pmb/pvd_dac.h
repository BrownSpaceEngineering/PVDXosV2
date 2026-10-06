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

// The divider and opamp map the 0-3.3 V DAC output to 0-1.2 V across the selected pixel
#define PVD_PIXEL_FULL_SCALE_MV 1200U

status_t pvd_dac_init(void);
bool pvd_dac_is_ready(void);
status_t pvd_dac_set_code(uint16_t code);
status_t pvd_dac_set_mv(uint32_t dac_mv);
status_t pvd_dac_set_pixel_mv(uint32_t pixel_mv);

#endif // PVD_DAC_H
