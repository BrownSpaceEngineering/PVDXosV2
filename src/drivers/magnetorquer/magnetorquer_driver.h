/**
 * magnetorquer_driver.h
 *
 * Driver for the three magnetorquer coils, each driven by a TI DRV8837C H-bridge (IN1/IN2/nSLEEP)
 *
 * Pin mapping (see PVDX-SAMD-PinConfig/README.md):
 *   X: IN1 = PA20 (TCC0/WO0), IN2 = PA21 (TCC0/WO1), nSLEEP = MTQ_X_SLP (PB30)
 *   Y: IN1 = PA22 (TCC0/WO2), IN2 = PA23 (TCC0/WO3), nSLEEP = MTQ_Y_SLP (PB31)
 *   Z: IN1 = PD20 (TCC1/WO0), IN2 = PD21 (TCC1/WO1), nSLEEP = MTQ_Z_SLP (PC27)
 *
 * Each bridge is PWM'd in slow decay (drive/brake): the "high" input is held at 100% and the other input is
 * high for (1 - duty) of the period, so the off-time shorts the coil through the low-side FETs instead of
 * dumping its energy back into VM. The average voltage across the coil is then duty * VM.
 * Positive voltage/current drives OUT1 -> OUT2 (DRV8837C "forward": IN1 = 1, IN2 = 0).
 *
 * Created: October 18, 2026
 * Authors: Zach Mahan
 */

#ifndef DRIVERS_MAGNETORQUER_H
#define DRIVERS_MAGNETORQUER_H

#include "globals.h"

typedef float_3d_t mag_torque_input_t;

// PWM frequency; the coil inductance low-pass filters the PWM into a DC current (DRV8837C allows up to 250 kHz)
#define MAGNETORQUER_PWM_FREQUENCY_HZ 20000U

// TODO: confirm the voltage on the DRV8837C VM pins with avionics. Defaults to a full 2S Li-ion battery so that, if
// VM is actually lower, the coils are under-driven rather than over-driven.
#define MAGNETORQUER_DEFAULT_SUPPLY_VOLTAGE_V 8.4f
#define MAGNETORQUER_MAX_SUPPLY_VOLTAGE_V 11.0f // DRV8837C recommended maximum VM

// DRV8837C rated output current; its overcurrent protection trips at 1.2 A (min)
#define MAGNETORQUER_MAX_CURRENT_A 1.0f

// Coil resistance model: R_coil(T) = R_ref * (1 + alpha * (T - T_ref))
// TODO: replace with the room-temperature resistances measured by ADCS
#define MAGNETORQUER_X_COIL_RESISTANCE_OHM 10.0f
#define MAGNETORQUER_Y_COIL_RESISTANCE_OHM 10.0f
#define MAGNETORQUER_Z_COIL_RESISTANCE_OHM 10.0f
#define MAGNETORQUER_COIL_REFERENCE_TEMP_C 20.0f
#define MAGNETORQUER_COIL_TEMP_COEFF_PER_C 0.00393f // Copper magnet wire

// DRV8837C HS + LS on-resistance, which is in series with the coil (datasheet 6.5 and figure 6)
#define MAGNETORQUER_DRIVER_RDS_ON_OHM 1.0f
#define MAGNETORQUER_DRIVER_RDS_ON_REFERENCE_TEMP_C 25.0f
#define MAGNETORQUER_DRIVER_RDS_ON_TEMP_COEFF_OHM_PER_C 0.0038f

typedef enum {
    MAGNETORQUER_X = 0,
    MAGNETORQUER_Y,
    MAGNETORQUER_Z,
    MAGNETORQUER_AXIS_COUNT,
} magnetorquer_axis_t;

// Hardware control
status_t init_magnetorquer_hardware(void);
status_t magnetorquer_set_voltages(float_3d_t voltages, float_3d_t *applied_voltages);
status_t magnetorquer_set_currents(float_3d_t currents, float temperature_c, float_3d_t *applied_voltages);
status_t magnetorquer_off(void);
status_t magnetorquer_set_supply_voltage(float supply_voltage);
float magnetorquer_get_supply_voltage(void);

// Pure model/conversion helpers (no hardware access)
float magnetorquer_resistance(magnetorquer_axis_t axis, float temperature_c);
float_3d_t magnetorquer_max_currents(float supply_voltage, float temperature_c);
float magnetorquer_clamp_voltage(float voltage, float supply_voltage);
void magnetorquer_compute_compare_values(float voltage, float supply_voltage, uint32_t period, uint32_t *in1_cc, uint32_t *in2_cc);

#endif
