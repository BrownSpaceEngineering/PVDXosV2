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

// ---- Divider (as-built, see thermistor_diagram.png) ----
// Three-element string from the rail to ground:
//   Vrail --[ R_T1 ]--+-- ADC tap --[ R_T2 ]-- NTC(thermistor) -- GND
// so Vtap/Vrail = (R_T2 + Rth) / (R_T1 + R_T2 + Rth).
#ifndef THERM_R_T1_OHMS
    #define THERM_R_T1_OHMS 15700.0f // top resistor (rail -> tap)
#endif
#ifndef THERM_R_T2_OHMS
    #define THERM_R_T2_OHMS 5780.0f // middle resistor (tap -> thermistor)
#endif

// ---- Thermistor Beta model (derived from the datasheet points
//      0 C = 32650.8 ohm, 60 C = 2487.1 ohm) ----
#define THERM_R0_OHMS 32650.8f // resistance at THERM_T0_K
#define THERM_T0_K    273.15f  // reference temperature (0 C) in kelvin
#define THERM_BETA    3905.65f // ln(R0/R60) / (1/T0 - 1/T60)

// ---- Heater control (bang-bang with hysteresis) ----
#define HEATER_SETPOINT_C 5.0f // target temperature
#define HEATER_HYST_C     1.0f // ON below (setpoint - hyst), OFF above (setpoint + hyst)

// Temporary threshold control on the ADC node voltage (higher voltage = colder on the
// as-built divider). Heater turns ON when the controlling thermistor is above this.
#define HEATER_ON_VOLTAGE 2.3f // volts

status_t init_thermistors(void);
status_t therm_read_raw(uint8_t index, uint16_t *code);
status_t therm_read_celsius(uint8_t index, float *temp_c);
status_t therm_read_all(float temps[THERM_COUNT]);

// Reads all thermistors and updates both heater enable lines (coldest-of-pair, hysteresis).
void heater_control_update(void);
// Last commanded state of a heater (0 or 1).
bool heater_is_on(uint8_t heater);

#endif // THERMISTOR_DRIVER_H
