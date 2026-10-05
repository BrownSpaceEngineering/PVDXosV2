/**
 * thermistor_driver.c
 *
 * Four NTC thermistors on ADC1 + two heater enable lines held at HEATER_SETPOINT_C.
 * See thermistor_driver.h for the wiring and model overview.
 */

#include "globals.h"

#include "thermistor_driver.h"

#include <math.h>

// Thermistor index -> ADC1 positive-input (MUXPOS AINx) value.
static const uint8_t THERM_AIN[THERM_COUNT] = {6, 7, 14, 15};

// Heater index -> enable GPIO, and the two thermistors that govern it.
static const uint32_t HEATER_PIN[THERM_HEATER_COUNT] = {HEATER_CTRL_1, HEATER_CTRL_2};
static const uint8_t HEATER_THERMS[THERM_HEATER_COUNT][2] = {{0, 1}, {2, 3}};

// Heaters are active-high enables (pin high = heater on).
#define HEATER_ON_LEVEL  true
#define HEATER_OFF_LEVEL false

// Persisted heater state (needed for hysteresis across the dead band).
static bool heater_on[THERM_HEATER_COUNT] = {false, false};

// ---------------------- Pure math helpers ----------------------

// Converts a ratiometric ADC code to thermistor resistance (ohms).
static float therm_resistance_from_code(uint16_t code) {
#if THERM_DIVIDER_HIGHSIDE
    // Vnode = Vref * Rfixed/(Rfixed+Rth)  ->  Rth = Rfixed*(FS/code - 1)
    return THERM_R_FIXED_OHMS * (THERM_ADC_FS / (float)code - 1.0f);
#else
    // Vnode = Vref * Rth/(Rfixed+Rth)  ->  Rth = Rfixed*code/(FS - code)
    return THERM_R_FIXED_OHMS * (float)code / (THERM_ADC_FS - (float)code);
#endif
}

// Converts thermistor resistance (ohms) to temperature (degrees C) via the Beta model.
static float therm_celsius_from_resistance(float r_ohms) {
    float inv_t = 1.0f / THERM_T0_K + logf(r_ohms / THERM_R0_OHMS) / THERM_BETA;
    return 1.0f / inv_t - 273.15f;
}

// ---------------------- ADC ----------------------

status_t init_thermistors(void) {
    // Ratiometric: reference = VDDANA (the rail that powers the divider), 12-bit.
    adc_sync_set_reference(&ADC_1, ADC_REFCTRL_REFSEL_INTVCC1_Val);
    adc_sync_set_resolution(&ADC_1, ADC_CTRLB_RESSEL_12BIT_Val);
    adc_sync_enable_channel(&ADC_1, 0); // HAL has no global enable; enable the channel

    // Heater enable lines: outputs, driven off.
    for (uint8_t h = 0; h < THERM_HEATER_COUNT; h++) {
        gpio_set_pin_function(HEATER_PIN[h], GPIO_PIN_FUNCTION_OFF);
        gpio_set_pin_direction(HEATER_PIN[h], GPIO_DIRECTION_OUT);
        gpio_set_pin_level(HEATER_PIN[h], HEATER_OFF_LEVEL);
        heater_on[h] = false;
    }

    return SUCCESS;
}

/**
 * \brief Reads one thermistor's averaged 12-bit ADC code.
 *
 * Switches the ADC mux to the thermistor's channel, discards one conversion for settling,
 * then averages THERM_ADC_AVG_SAMPLES conversions.
 */
status_t therm_read_raw(uint8_t index, uint16_t *code) {
    if (index >= THERM_COUNT || code == NULL) {
        return ERROR_BAD_TARGET;
    }

    if (adc_sync_set_inputs(&ADC_1, THERM_AIN[index], ADC_INPUTCTRL_MUXNEG_GND_Val, 0) != ERR_NONE) {
        warning("thermistor: set_inputs failed (idx=%u)\n", index);
        return ERROR_READ_FAILED;
    }

    uint8_t buf[2];
    // Settling conversion after switching the mux (discarded).
    if (adc_sync_read_channel(&ADC_1, 0, buf, sizeof(buf)) < 0) {
        warning("thermistor: ADC read failed (idx=%u)\n", index);
        return ERROR_READ_FAILED;
    }

    uint32_t acc = 0;
    for (uint8_t i = 0; i < THERM_ADC_AVG_SAMPLES; i++) {
        if (adc_sync_read_channel(&ADC_1, 0, buf, sizeof(buf)) < 0) {
            warning("thermistor: ADC read failed (idx=%u)\n", index);
            return ERROR_READ_FAILED;
        }
        acc += (uint32_t)buf[0] | ((uint32_t)buf[1] << 8);
    }

    *code = (uint16_t)(acc / THERM_ADC_AVG_SAMPLES);
    return SUCCESS;
}

status_t therm_read_celsius(uint8_t index, float *temp_c) {
    if (temp_c == NULL) {
        return ERROR_BAD_TARGET;
    }

    uint16_t code = 0;
    ret_err_status(therm_read_raw(index, &code), "thermistor: raw read failed");

    // Guard the divider math against a railed ADC (open/short thermistor).
    if (code == 0 || (float)code >= THERM_ADC_FS) {
        warning("thermistor: idx=%u code railed (%u) - open/short?\n", index, code);
        return ERROR_SANITY_CHECK_FAILED;
    }

    *temp_c = therm_celsius_from_resistance(therm_resistance_from_code(code));
    return SUCCESS;
}

status_t therm_read_all(float temps[THERM_COUNT]) {
    if (temps == NULL) {
        return ERROR_BAD_TARGET;
    }
    status_t worst = SUCCESS;
    for (uint8_t i = 0; i < THERM_COUNT; i++) {
        temps[i] = 0.0f;
        status_t rc = therm_read_celsius(i, &temps[i]);
        if (rc != SUCCESS) {
            worst = rc;
        }
    }
    return worst;
}

// ---------------------- Heater control ----------------------

void heater_control_update(void) {
    float temps[THERM_COUNT];
    status_t ok[THERM_COUNT];
    for (uint8_t i = 0; i < THERM_COUNT; i++) {
        temps[i] = 0.0f;
        ok[i] = therm_read_celsius(i, &temps[i]);
    }

    for (uint8_t h = 0; h < THERM_HEATER_COUNT; h++) {
        uint8_t a = HEATER_THERMS[h][0];
        uint8_t b = HEATER_THERMS[h][1];

        // Coldest valid thermistor of the pair drives the decision. If a reading failed,
        // fall back to the other; if both failed, force the heater off (fail safe).
        bool have = false;
        float ctrl = 0.0f;
        if (ok[a] == SUCCESS) {
            ctrl = temps[a];
            have = true;
        }
        if (ok[b] == SUCCESS) {
            ctrl = have ? fminf(ctrl, temps[b]) : temps[b];
            have = true;
        }

        if (!have) {
            warning("heater %u: both thermistors failed - forcing OFF\n", h + 1);
            heater_on[h] = false;
        } else if (ctrl < HEATER_SETPOINT_C - HEATER_HYST_C) {
            heater_on[h] = true;
        } else if (ctrl > HEATER_SETPOINT_C + HEATER_HYST_C) {
            heater_on[h] = false;
        }
        // else: within the hysteresis band -> hold previous state.

        gpio_set_pin_level(HEATER_PIN[h], heater_on[h] ? HEATER_ON_LEVEL : HEATER_OFF_LEVEL);
    }
}

bool heater_is_on(uint8_t heater) {
    if (heater >= THERM_HEATER_COUNT) {
        return false;
    }
    return heater_on[heater];
}
