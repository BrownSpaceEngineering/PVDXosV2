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

// Converts a ratiometric ADC code to thermistor resistance (ohms) for the as-built
// 3-element divider (Vrail - R_T1 - tap - R_T2 - NTC - GND):
//   k = code/FS = Vtap/Vrail = (R_T2 + Rth) / (R_T1 + R_T2 + Rth)
//   =>  Rth = k*R_T1/(1 - k) - R_T2
static float therm_resistance_from_code(uint16_t code) {
    float k = (float)code / THERM_ADC_FS;
    return k * THERM_R_T1_OHMS / (1.0f - k) - THERM_R_T2_OHMS;
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

// ---------------------- Fault detection (see THERMISTOR_FAULT_PLAN.md) ----------------------

// Median of n floats (n <= THERM_COUNT). Copies and sorts in place; for even n returns the
// average of the two middle values (so a single wild outlier sorts to an end and cannot
// drag the reference off the good cluster).
static float therm_median(const float *v, uint8_t n) {
    if (n == 0) {
        return 0.0f;
    }
    float s[THERM_COUNT];
    for (uint8_t i = 0; i < n; i++) {
        s[i] = v[i];
    }
    for (uint8_t i = 1; i < n; i++) { // insertion sort; n is tiny
        float key = s[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && s[j] > key) {
            s[j + 1] = s[j];
            j--;
        }
        s[j + 1] = key;
    }
    if (n & 1u) {
        return s[n / 2];
    }
    return 0.5f * (s[n / 2 - 1] + s[n / 2]);
}

// Electrical validity guard: a railed/short divider yields a k = code/FS that drives the
// Rth model to a divide-by-zero or a non-physical (<= 0) resistance. Reject those before
// they can poison the consensus median (open NTC -> k -> 1; tap short -> Rth <= 0).
static bool therm_electrically_valid(uint16_t code) {
    if (code == 0) {
        return false;
    }
    if ((float)code / THERM_ADC_FS >= THERM_K_OPEN) {
        return false;
    }
    return therm_resistance_from_code(code) > 0.0f;
}

// ---------------------- Heater control ----------------------

void heater_control_update(void) {
    // Per THERMISTOR_FAULT_PLAN.md: (1) read all four, apply the electrical guard, then flag
    // consensus outliers; (2) replace a broken sensor with its pair partner, or - when both
    // of a pair are broken - with the four-way consensus of the remaining good sensors.
    // Control still runs on the ADC node voltage (higher voltage = colder on this divider).
    float volts[THERM_COUNT];
    bool good[THERM_COUNT];
    float valid_v[THERM_COUNT];
    uint8_t n_valid = 0;

    // ---- (1a) Read + electrical guard ----
    for (uint8_t i = 0; i < THERM_COUNT; i++) {
        uint16_t code = 0;
        bool elec_ok = (therm_read_raw(i, &code) == SUCCESS) && therm_electrically_valid(code);
        volts[i] = (float)code / THERM_ADC_FS * 3.3f; // ratiometric, VREF = VDDANA
        good[i] = elec_ok; // provisional; the consensus test below may clear it
        if (elec_ok) {
            valid_v[n_valid++] = volts[i];
        }
    }

    // ---- (1b) Consensus outlier test over the electrically-valid set ----
    if (n_valid > 0) {
        float med = therm_median(valid_v, n_valid);
        for (uint8_t i = 0; i < THERM_COUNT; i++) {
            if (good[i] && fabsf(volts[i] - med) > THERM_DELTA_FAULT_V) {
                good[i] = false;
                warning("thermistor %u flagged broken: %d mV vs median %d mV\n", i,
                        (int)(volts[i] * 1000.0f), (int)(med * 1000.0f));
            }
        }
    }

    // Four-way consensus (mean of all good sensors): the fallback when a whole pair is dead.
    float good_sum = 0.0f;
    uint8_t n_good = 0;
    for (uint8_t i = 0; i < THERM_COUNT; i++) {
        if (good[i]) {
            good_sum += volts[i];
            n_good++;
        }
    }
    float consensus_v = (n_good > 0) ? good_sum / (float)n_good : 0.0f;

    // ---- (2) Per-heater control with pair-partner / consensus replacement ----
    for (uint8_t h = 0; h < THERM_HEATER_COUNT; h++) {
        uint8_t a = HEATER_THERMS[h][0];
        uint8_t b = HEATER_THERMS[h][1];

        bool have = false;
        float ctrl = 0.0f;
        if (good[a] && good[b]) {
            ctrl = fmaxf(volts[a], volts[b]); // coldest valid of the pair
            have = true;
        } else if (good[a]) {
            ctrl = volts[a]; // pair partner stands in for the broken one
            have = true;
        } else if (good[b]) {
            ctrl = volts[b]; // pair partner stands in for the broken one
            have = true;
        } else if (n_good > 0) {
            ctrl = consensus_v; // whole pair broken -> four-way consensus
            have = true;
            warning("heater %u: both pair thermistors broken - using consensus\n", h + 1);
        }

        if (have) {
            heater_on[h] = (ctrl > HEATER_ON_VOLTAGE);
        } else {
            // All four sensors invalid: no consensus available. Hold the last-good state.
            warning("heater %u: no valid thermistors - holding last state (%s)\n", h + 1,
                    heater_on[h] ? "ON" : "OFF");
        }

        gpio_set_pin_level(HEATER_PIN[h], heater_on[h] ? HEATER_ON_LEVEL : HEATER_OFF_LEVEL);
    }
}

bool heater_is_on(uint8_t heater) {
    if (heater >= THERM_HEATER_COUNT) {
        return false;
    }
    return heater_on[heater];
}
