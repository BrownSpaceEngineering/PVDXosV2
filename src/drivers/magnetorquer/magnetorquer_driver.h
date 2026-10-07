#ifndef MAGNETORQUER_DRIVER_H
#define MAGNETORQUER_DRIVER_H

/**
 * magnetorquer_driver.h
 *
 * Bare-metal driver for three DRV8837CDSGR H-bridges (X/Y/Z magnetorquers). Each axis is
 * driven by two PWM lines (IN1/IN2) plus an active-low sleep enable (nSLEEP):
 *
 *   Axis  IN1 (WO/CC)         IN2 (WO/CC)         nSLEEP
 *   X     PA20 TCC0_WO0 CC0   PA21 TCC0_WO1 CC1   MTQ_X_SLP (PB30)
 *   Y     PA22 TCC0_WO2 CC2   PA23 TCC0_WO3 CC3   MTQ_Y_SLP (PB31)
 *   Z     PD20 TCC1_WO0 CC0   PD21 TCC1_WO1 CC1   MTQ_Z_SLP (PC27)
 *
 * Drive is sign-magnitude: +duty -> PWM on IN1, IN2 low; -duty -> IN1 low, PWM on IN2;
 * 0 -> both low (coast). nSLEEP is active-low (driven high only while commanding a nonzero
 * duty). Per-WO duty comes from writing each TCC compare register directly (the ASF hal_pwm
 * only drives one duty per timer); this is valid because WEXCTRL stays at reset
 * (OTMX=0 -> WO[n]<-CC[n], DTIEN=0 -> no complementary pairing) and WAVEGEN=NPWM.
 */

#include "stdint.h"
#include "globals.h"
#include "atmel_start.h"
#include "logging.h"

typedef enum { MTQ_X = 0, MTQ_Y, MTQ_Z } mtq_axis_t;

#define MTQ_AXIS_COUNT 3

status_t init_magnetorquers(void);

// Commands a signed PWM duty on an axis. `duty` is clamped to [-1.0, 1.0]; sign selects the
// coil-current direction, magnitude selects the PWM duty. 0 coasts (both inputs low).
void mtq_set(mtq_axis_t axis, float duty);
void mtq_set_all(float x, float y, float z);

// Coast an axis (or all) and put its driver to sleep.
void mtq_stop(mtq_axis_t axis);
void mtq_stop_all(void);

// Last commanded duty for an axis (for logging/tests).
float mtq_get_duty(mtq_axis_t axis);

#endif // MAGNETORQUER_DRIVER_H
