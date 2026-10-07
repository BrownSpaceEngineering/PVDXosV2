#ifndef PHOTODIODE_DRIVER_H
#define PHOTODIODE_DRIVER_H

/**
 * photodiode_driver.h
 *
 * Bare-metal driver for 18 photodiode analog channels (coarse sun sensing), read via the
 * two ADCs. Pins are already ADC-pinmuxed by the ASF (ADC_0/1_PORT_init):
 *   PHD_1..PHD_15 -> ADC0 AIN1..AIN15   (index == AIN)
 *   PHD_0  -> ADC1 AIN10, PHD_16 -> ADC1 AIN4, PHD_17 -> ADC1 AIN5
 *
 * Reads are ratiometric (reference = VDDANA). ADC1 is shared with the thermistor driver;
 * both set the same config and select inputs per read, so interleaved use is safe.
 */

#include "stdint.h"
#include "globals.h"
#include "atmel_start.h"
#include "logging.h"

#define PHD_COUNT           18
#define PHD_ADC_FS          4095.0f // 12-bit full scale
#define PHD_VREF_V          3.3f    // VDDANA (for code -> voltage)
#define PHD_ADC_AVG_SAMPLES 4

status_t init_photodiodes(void);
status_t phd_read_raw(uint8_t index, uint16_t *code);
status_t phd_read_voltage(uint8_t index, float *volts);
status_t phd_read_all(uint16_t codes[PHD_COUNT]);

#endif // PHOTODIODE_DRIVER_H
