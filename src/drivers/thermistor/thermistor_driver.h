#ifndef THERMISTOR_DRIVER_H
#define THERMISTOR_DRIVER_H

/**
 * thermistor_driver.h
 *
 * Bare-metal driver for four NTC thermistors (ADC1) plus two heater enable lines.
 * The heaters are held in closed-loop bang-bang control: on at or below HEATER_ON_C,
 * off at or above HEATER_OFF_C.
 *
 * Thermistor pins are already ADC1-pinmuxed by the ASF (ADC_1_PORT_init):
 *   THERM_0 = PB04 -> AIN6, THERM_1 = PB05 -> AIN7,
 *   THERM_2 = PB06 -> AIN8, THERM_3 = PB07 -> AIN9.
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
#define THERM_VREF_V          3.3f    // VDDANA (for code -> voltage)
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
// Control runs on the ADC node voltage (higher voltage = colder on the as-built divider). These
// temperatures are converted to node-voltage thresholds through the divider and Beta model above
// (4 C -> ~2.222 V, 6 C -> ~2.161 V); between the two the heater keeps its current state.
#define HEATER_ON_C  4.0f // heater turns ON at or below this
#define HEATER_OFF_C 6.0f // heater turns OFF at or above this

// ---- Single-fault tolerance (see THERMISTOR_FAULT_PLAN.md) ----
// Electrical guard: k = code/FS at/above this means the divider has railed (open NTC or
// tap shorted to the rail), which drives the Rth model to a divide-by-zero. Reject such a
// reading before it can poison the consensus median.
#define THERM_K_OPEN 0.99f
// Consensus outlier band, in volts (the control domain): a reading more than this away from
// the median of the electrically-valid sensors is flagged broken. PROVISIONAL - tune from
// the measured real spread; ~0.25 V is roughly an 8 C gap near the ~5 C setpoint.
#define THERM_DELTA_FAULT_V 0.25f

status_t init_thermistors(void);
status_t therm_read_raw(uint8_t index, uint16_t *code);
status_t therm_read_celsius(uint8_t index, float *temp_c);
// Reads all thermistors once. codes[i] is 0 (electrically invalid) and temps[i] is 0 for a
// thermistor that failed to read or is out of range.
status_t therm_read_all(uint16_t codes[THERM_COUNT], float temps[THERM_COUNT]);

// Updates both heater enable lines (coldest-of-pair) from codes produced by therm_read_all().
void heater_control_update(const uint16_t codes[THERM_COUNT]);
// Last commanded state of a heater (0 or 1).
bool heater_is_on(uint8_t heater);

#endif // THERMISTOR_DRIVER_H
