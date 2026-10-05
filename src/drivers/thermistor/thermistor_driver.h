#ifndef THERMISTOR_DRIVER_H
#define THERMISTOR_DRIVER_H

/**
 * thermistor_driver.h
 *
 * Bare-metal driver for four NTC thermistors (ADC1) plus two heater enable lines.
 * The heaters are held in closed-loop bang-bang control to maintain the monitored
 * zones at HEATER_SETPOINT_C.
 *
 * Thermistor pins are already ADC1-pinmuxed by the ASF (ADC_1_PORT_init):
 *   THERM_0 = PB04 -> AIN6, THERM_1 = PB05 -> AIN7,
 *   THERM_2 = PD00 -> AIN14, THERM_3 = PD01 -> AIN15.
 * Heaters: HEATER_CTRL_1 = PC23 (thermistors 0,1), HEATER_CTRL_2 = PC24 (thermistors 2,3).
 *
 * The ADC is read ratiometrically (reference = VDDANA, same rail that powers the divider),
 * so temperature depends only on code/full-scale, not the absolute 3V3 value.
 */

#include "stdint.h"
#include "globals.h"
#include "atmel_start.h"
#include "logging.h"

#define THERM_COUNT        4
#define THERM_HEATER_COUNT 2

// ---- ADC ----
#define THERM_ADC_FS          4095.0f // 12-bit full scale
#define THERM_ADC_AVG_SAMPLES 4       // samples averaged per reading (after a settling discard)

// ---- Divider ----
// 1 = thermistor on the high side (to 3V3), fixed resistor to GND.
// 0 = thermistor on the low side (to GND), fixed resistor to 3V3.
#ifndef THERM_DIVIDER_HIGHSIDE
    #define THERM_DIVIDER_HIGHSIDE 1
#endif
#ifndef THERM_R_FIXED_OHMS
    #define THERM_R_FIXED_OHMS 10000.0f // divider fixed resistor (adjust to match the board)
#endif

// ---- Thermistor Beta model (derived from the datasheet points
//      0 C = 32650.8 ohm, 60 C = 2487.1 ohm) ----
#define THERM_R0_OHMS 32650.8f // resistance at THERM_T0_K
#define THERM_T0_K    273.15f  // reference temperature (0 C) in kelvin
#define THERM_BETA    3905.65f // ln(R0/R60) / (1/T0 - 1/T60)

// ---- Heater control (bang-bang with hysteresis) ----
#define HEATER_SETPOINT_C 5.0f // target temperature
#define HEATER_HYST_C     1.0f // ON below (setpoint - hyst), OFF above (setpoint + hyst)

status_t init_thermistors(void);
status_t therm_read_raw(uint8_t index, uint16_t *code);
status_t therm_read_celsius(uint8_t index, float *temp_c);
status_t therm_read_all(float temps[THERM_COUNT]);

// Reads all thermistors and updates both heater enable lines (coldest-of-pair, hysteresis).
void heater_control_update(void);
// Last commanded state of a heater (0 or 1).
bool heater_is_on(uint8_t heater);

#endif // THERMISTOR_DRIVER_H
