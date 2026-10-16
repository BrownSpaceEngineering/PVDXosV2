/**
 * pvd_dac.c
 *
 * Drives PVD_CRNT (PA02, DAC VOUT0), the control voltage for the pixel measurement circuit.
 */

#include "pvd_dac.h"

#include "hal_dac_sync.h"

#define PVD_DAC_READY_TIMEOUT 100000U // Polling iterations to wait for the DAC channel to start up

/*
 * Channel settings that override the Atmel START configuration (hpl_dac_config.h), which leaves CCTRL at CC100K and
 * refresh off. CCTRL must match the 12 MHz GCLK_DAC, and since the channel holds a static level for tens of ms per
 * sweep step, its output must be refreshed or it droops. REFRESH n gives a period of n * 30 us (0 = off, 1 reserved).
 * TODO: move these into the Atmel START project (PVDX-SAMD-PinConfig) so the generated config matches.
 */
#define PVD_DAC_CCTRL DAC_DACCTRL_CCTRL_CC12M_Val
#define PVD_DAC_REFRESH 2U

/**
 * Applies the channel settings above, enables the DAC channel, waits for it to start up and sets the output to 0 V.
 * system_init() initializes the DAC but leaves the channel disabled.
 */
status_t pvd_dac_init(void) {
    // DACCTRL is enable-protected; dac_sync_enable_channel() re-enables the DAC afterwards
    hri_dac_clear_CTRLA_ENABLE_bit(DAC);
    hri_dac_write_DACCTRL_CCTRL_bf(DAC, PVD_DAC_CHANNEL, PVD_DAC_CCTRL);
    hri_dac_write_DACCTRL_REFRESH_bf(DAC, PVD_DAC_CHANNEL, PVD_DAC_REFRESH);

    if (dac_sync_enable_channel(&PVD_DAC, PVD_DAC_CHANNEL) != 0) {
        warning("pvd_dac: failed to enable channel %u\n", PVD_DAC_CHANNEL);
        return ERROR_NOT_READY;
    }

    uint32_t timeout = PVD_DAC_READY_TIMEOUT;
    while (!pvd_dac_is_ready()) {
        if (--timeout == 0) {
            warning("pvd_dac: channel %u not ready after enable\n", PVD_DAC_CHANNEL);
            return ERROR_NOT_READY;
        }
    }

    return pvd_dac_set_code(0);
}

// True once the channel has finished its start-up after pvd_dac_init()
bool pvd_dac_is_ready(void) {
    return hri_dac_get_STATUS_READY0_bit(DAC);
}

status_t pvd_dac_set_code(uint16_t code) {
    if (code > PVD_DAC_MAX_CODE) {
        code = PVD_DAC_MAX_CODE;
    }
    if (dac_sync_write(&PVD_DAC, PVD_DAC_CHANNEL, &code, 1) != 0) {
        return ERROR_NOT_READY; // Channel not enabled; call pvd_dac_init() first
    }
    return SUCCESS;
}

/**
 * Sets the DAC output voltage (0 to PVD_DAC_VREF_MV), clamping out-of-range values.
 */
status_t pvd_dac_set_mv(uint32_t dac_mv) {
    if (dac_mv > PVD_DAC_VREF_MV) {
        dac_mv = PVD_DAC_VREF_MV;
    }
    return pvd_dac_set_code((uint16_t)((dac_mv * PVD_DAC_MAX_CODE + PVD_DAC_VREF_MV / 2) / PVD_DAC_VREF_MV));
}

/**
 * Sets the opamp output FOLLOW (0 to PVD_PIXEL_FULL_SCALE_MV), clamping out-of-range values. The selected pixel
 * sits at this voltage plus the shunt drop; read the actual pixel voltage from the INA226 bus voltage.
 */
status_t pvd_dac_set_pixel_mv(uint32_t pixel_mv) {
    if (pixel_mv > PVD_PIXEL_FULL_SCALE_MV) {
        pixel_mv = PVD_PIXEL_FULL_SCALE_MV;
    }
    return pvd_dac_set_mv((pixel_mv * PVD_DAC_VREF_MV + PVD_PIXEL_FULL_SCALE_MV / 2) / PVD_PIXEL_FULL_SCALE_MV);
}
