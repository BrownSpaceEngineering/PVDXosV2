/**
 * magnetorquer_driver.c
 *
 * Driver for the three magnetorquer coils: converts commanded coil voltages (or currents) into slow-decay PWM on the
 * DRV8837C H-bridges. TCC0 drives X and Y, TCC1 drives Z. ASF configures the pins and clocks; this driver overrides
 * the ASF prescaler and period to get MAGNETORQUER_PWM_FREQUENCY_HZ and drives every compare channel directly, since
 * the ASF PWM API only controls one channel per TCC.
 *
 * Not thread-safe: only one task (ADCS) should command the magnetorquers.
 *
 * Created: October 18, 2026
 * Authors: Zach Mahan
 */

#include "magnetorquer_driver.h"

#include <peripheral_clk_config.h>

#include "atmel_start.h"
#include "logging.h"

// The counter counts 0..PER, so a compare value of MAGNETORQUER_PWM_PERIOD_COUNTS (PER + 1) is never matched and
// holds the output high for the whole period, while 0 holds it low (datasheet 49.6.2.5.5)
#define MAGNETORQUER_PWM_PERIOD_COUNTS (CONF_GCLK_TCC0_FREQUENCY / MAGNETORQUER_PWM_FREQUENCY_HZ)

_Static_assert(CONF_GCLK_TCC0_FREQUENCY == CONF_GCLK_TCC1_FREQUENCY, "magnetorquer TCC0 and TCC1 must share a clock frequency");
_Static_assert(CONF_GCLK_TCC0_FREQUENCY % MAGNETORQUER_PWM_FREQUENCY_HZ == 0, "magnetorquer PWM frequency must divide the TCC clock");
_Static_assert(MAGNETORQUER_PWM_PERIOD_COUNTS >= 100, "magnetorquer PWM resolution is too coarse");

typedef struct {
    struct pwm_descriptor *pwm; // ASF descriptor that owns the TCC
    uint8_t in1_channel;        // Compare channel routed to IN1 (WO[n] = CC[n] with the default output matrix)
    uint8_t in2_channel;        // Compare channel routed to IN2
    uint8_t sleep_pin;          // nSLEEP (active low)
    float coil_resistance;      // Coil resistance at MAGNETORQUER_COIL_REFERENCE_TEMP_C
} magnetorquer_channel_t;

static const magnetorquer_channel_t magnetorquer_channels[MAGNETORQUER_AXIS_COUNT] = {
    [MAGNETORQUER_X] = {&MAGNETORQUER_1, 0, 1, MTQ_X_SLP, MAGNETORQUER_X_COIL_RESISTANCE_OHM},
    [MAGNETORQUER_Y] = {&MAGNETORQUER_1, 2, 3, MTQ_Y_SLP, MAGNETORQUER_Y_COIL_RESISTANCE_OHM},
    [MAGNETORQUER_Z] = {&MAGNETORQUER_2, 0, 1, MTQ_Z_SLP, MAGNETORQUER_Z_COIL_RESISTANCE_OHM},
};

static struct pwm_descriptor *const magnetorquer_pwms[] = {&MAGNETORQUER_1, &MAGNETORQUER_2};
#define MAGNETORQUER_PWM_COUNT (sizeof(magnetorquer_pwms) / sizeof(magnetorquer_pwms[0]))

static bool magnetorquer_initialized = false;
static bool magnetorquer_awake = false;
static float magnetorquer_supply_voltage = MAGNETORQUER_DEFAULT_SUPPLY_VOLTAGE_V;

static float *axis_component(float_3d_t *v, magnetorquer_axis_t axis) {
    switch (axis) {
        case MAGNETORQUER_X:
            return &v->x;
        case MAGNETORQUER_Y:
            return &v->y;
        default:
            return &v->z;
    }
}

static float clampf(float value, float limit) {
    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

static void set_sleep_pins(bool awake) {
    for (int axis = 0; axis < MAGNETORQUER_AXIS_COUNT; axis++) {
        gpio_set_pin_level(magnetorquer_channels[axis].sleep_pin, awake);
    }
    magnetorquer_awake = awake;
}

// Lock (or unlock) the CCBUF -> CC transfer so a new set of compare values takes effect on the same PWM cycle
static void lock_compare_updates(bool lock) {
    for (size_t i = 0; i < MAGNETORQUER_PWM_COUNT; i++) {
        void *const hw = magnetorquer_pwms[i]->device.hw;
        if (lock) {
            hri_tcc_set_CTRLB_LUPD_bit(hw);
        } else {
            hri_tcc_clear_CTRLB_LUPD_bit(hw);
        }
        hri_tcc_wait_for_sync(hw, TCC_SYNCBUSY_CTRLB);
    }
}

static void write_compare_values(const uint32_t in1_cc[MAGNETORQUER_AXIS_COUNT], const uint32_t in2_cc[MAGNETORQUER_AXIS_COUNT]) {
    lock_compare_updates(true);
    for (int axis = 0; axis < MAGNETORQUER_AXIS_COUNT; axis++) {
        const magnetorquer_channel_t *ch = &magnetorquer_channels[axis];
        hri_tcc_write_CCBUF_reg(ch->pwm->device.hw, ch->in1_channel, in1_cc[axis]);
        hri_tcc_write_CCBUF_reg(ch->pwm->device.hw, ch->in2_channel, in2_cc[axis]);
    }
    lock_compare_updates(false);
}

/**
 * \fn init_magnetorquer_hardware
 *
 * \brief Reconfigure the ASF-initialized TCCs for MAGNETORQUER_PWM_FREQUENCY_HZ and start them with every bridge
 *        asleep and both inputs low (coast)
 *
 * \returns status_t SUCCESS if initialization was successful
 */
status_t init_magnetorquer_hardware(void) {
    set_sleep_pins(false);

    for (size_t i = 0; i < MAGNETORQUER_PWM_COUNT; i++) {
        void *const hw = magnetorquer_pwms[i]->device.hw;
        // PRESCALER is enable-protected
        if (hri_tcc_get_CTRLA_ENABLE_bit(hw)) {
            hri_tcc_clear_CTRLA_ENABLE_bit(hw);
            hri_tcc_wait_for_sync(hw, TCC_SYNCBUSY_ENABLE);
        }
        hri_tcc_write_CTRLA_PRESCALER_bf(hw, TCC_CTRLA_PRESCALER_DIV1_Val);
        hri_tcc_write_PER_reg(hw, MAGNETORQUER_PWM_PERIOD_COUNTS - 1);
    }

    for (int axis = 0; axis < MAGNETORQUER_AXIS_COUNT; axis++) {
        const magnetorquer_channel_t *ch = &magnetorquer_channels[axis];
        hri_tcc_write_CC_reg(ch->pwm->device.hw, ch->in1_channel, 0);
        hri_tcc_write_CC_reg(ch->pwm->device.hw, ch->in2_channel, 0);
    }

    for (size_t i = 0; i < MAGNETORQUER_PWM_COUNT; i++) {
        void *const hw = magnetorquer_pwms[i]->device.hw;
        hri_tcc_set_CTRLA_ENABLE_bit(hw);
        hri_tcc_wait_for_sync(hw, TCC_SYNCBUSY_ENABLE);
    }

    magnetorquer_initialized = true;
    info("magnetorquer: Initialized at %u Hz PWM (%u steps), VM = %d mV\n", MAGNETORQUER_PWM_FREQUENCY_HZ, MAGNETORQUER_PWM_PERIOD_COUNTS,
         (int)(magnetorquer_supply_voltage * 1000.0f));
    return SUCCESS;
}

/**
 * \fn magnetorquer_set_voltages
 *
 * \brief Command the average voltage across each coil. Voltages are clamped to +/- the supply voltage. Wakes the
 *        bridges if they were put to sleep by `magnetorquer_off()`; a zero voltage brakes the coil.
 *
 * \param voltages signed coil voltages (V); positive drives current OUT1 -> OUT2
 * \param applied_voltages if non-NULL, receives the voltages actually commanded after clamping and quantization
 *
 * \returns status_t SUCCESS, or ERROR_NOT_READY if the driver has not been initialized
 */
status_t magnetorquer_set_voltages(float_3d_t voltages, float_3d_t *applied_voltages) {
    if (!magnetorquer_initialized) {
        warning("magnetorquer: Attempting to set voltages before initializing magnetorquers\n");
        return ERROR_NOT_READY;
    }

    const float supply_voltage = magnetorquer_supply_voltage;
    uint32_t in1_cc[MAGNETORQUER_AXIS_COUNT];
    uint32_t in2_cc[MAGNETORQUER_AXIS_COUNT];
    for (int axis = 0; axis < MAGNETORQUER_AXIS_COUNT; axis++) {
        magnetorquer_compute_compare_values(*axis_component(&voltages, axis), supply_voltage, MAGNETORQUER_PWM_PERIOD_COUNTS, &in1_cc[axis],
                                            &in2_cc[axis]);
        if (applied_voltages != NULL) {
            // Drive time is the fraction of the period the two inputs differ
            float drive_counts = (float)in1_cc[axis] - (float)in2_cc[axis];
            *axis_component(applied_voltages, axis) = drive_counts * supply_voltage / (float)MAGNETORQUER_PWM_PERIOD_COUNTS;
        }
    }

    write_compare_values(in1_cc, in2_cc);
    if (!magnetorquer_awake) {
        // The bridges take up to 30 us (under one PWM period) to wake
        set_sleep_pins(true);
    }
    return SUCCESS;
}

/**
 * \fn magnetorquer_set_currents
 *
 * \brief Command a DC current through each coil, using the temperature-dependent coil plus driver resistance to pick
 *        the voltage. Currents are clamped to +/- MAGNETORQUER_MAX_CURRENT_A, and the resulting voltages to the supply.
 *
 * \param currents signed coil currents (A), e.g. the output of the ADCS current converter
 * \param temperature_c magnetorquer temperature (degrees C)
 * \param applied_voltages if non-NULL, receives the voltages actually commanded
 *
 * \returns status_t result of `magnetorquer_set_voltages()`
 */
status_t magnetorquer_set_currents(float_3d_t currents, float temperature_c, float_3d_t *applied_voltages) {
    float_3d_t voltages;
    for (int axis = 0; axis < MAGNETORQUER_AXIS_COUNT; axis++) {
        float current = clampf(*axis_component(&currents, axis), MAGNETORQUER_MAX_CURRENT_A);
        *axis_component(&voltages, axis) = current * magnetorquer_resistance(axis, temperature_c);
    }
    return magnetorquer_set_voltages(voltages, applied_voltages);
}

/**
 * \fn magnetorquer_off
 *
 * \brief Turn every coil off by letting both bridge inputs fall low (coast) and putting the bridges to sleep (Hi-Z),
 *        e.g. for the ADCS magnetometer quiet window. The next `magnetorquer_set_*()` call wakes them again.
 *
 * \returns status_t SUCCESS, or ERROR_NOT_READY if the driver has not been initialized
 */
status_t magnetorquer_off(void) {
    if (!magnetorquer_initialized) {
        return ERROR_NOT_READY;
    }
    const uint32_t zero[MAGNETORQUER_AXIS_COUNT] = {0};
    write_compare_values(zero, zero);
    set_sleep_pins(false);
    return SUCCESS;
}

/**
 * \fn magnetorquer_set_supply_voltage
 *
 * \brief Set the H-bridge supply voltage (VM) used to convert voltages into duty cycles, e.g. from a measured rail
 *
 * \param supply_voltage VM (V), in (0, MAGNETORQUER_MAX_SUPPLY_VOLTAGE_V]
 *
 * \returns status_t SUCCESS, or ERROR_BAD_ARGS if the voltage is out of range
 */
status_t magnetorquer_set_supply_voltage(float supply_voltage) {
    if (!(supply_voltage > 0.0f && supply_voltage <= MAGNETORQUER_MAX_SUPPLY_VOLTAGE_V)) {
        warning("magnetorquer: Rejecting supply voltage of %d mV\n", (int)(supply_voltage * 1000.0f));
        return ERROR_BAD_ARGS;
    }
    magnetorquer_supply_voltage = supply_voltage;
    return SUCCESS;
}

float magnetorquer_get_supply_voltage(void) {
    return magnetorquer_supply_voltage;
}

/**
 * \fn magnetorquer_resistance
 *
 * \brief Total resistance seen by the PWM'd supply for one axis: coil plus DRV8837C on-resistance, both linear in
 *        temperature
 *
 * \param axis magnetorquer axis
 * \param temperature_c magnetorquer temperature (degrees C)
 *
 * \returns float resistance (ohms)
 */
float magnetorquer_resistance(magnetorquer_axis_t axis, float temperature_c) {
    const float coil = magnetorquer_channels[axis].coil_resistance *
        (1.0f + MAGNETORQUER_COIL_TEMP_COEFF_PER_C * (temperature_c - MAGNETORQUER_COIL_REFERENCE_TEMP_C));
    const float driver = MAGNETORQUER_DRIVER_RDS_ON_OHM +
        MAGNETORQUER_DRIVER_RDS_ON_TEMP_COEFF_OHM_PER_C * (temperature_c - MAGNETORQUER_DRIVER_RDS_ON_REFERENCE_TEMP_C);
    return coil + driver;
}

/**
 * \fn magnetorquer_max_currents
 *
 * \brief Largest current each coil can be guaranteed at a given supply voltage and temperature. Passing the hottest
 *        expected temperature gives a current limit that holds across the whole operating range.
 *
 * \param supply_voltage VM (V)
 * \param temperature_c magnetorquer temperature (degrees C)
 *
 * \returns float_3d_t per-axis current limits (A)
 */
float_3d_t magnetorquer_max_currents(float supply_voltage, float temperature_c) {
    float_3d_t limits;
    for (int axis = 0; axis < MAGNETORQUER_AXIS_COUNT; axis++) {
        float limit = supply_voltage / magnetorquer_resistance(axis, temperature_c);
        *axis_component(&limits, axis) = limit < MAGNETORQUER_MAX_CURRENT_A ? limit : MAGNETORQUER_MAX_CURRENT_A;
    }
    return limits;
}

float magnetorquer_clamp_voltage(float voltage, float supply_voltage) {
    return clampf(voltage, supply_voltage);
}

/**
 * \fn magnetorquer_compute_compare_values
 *
 * \brief Convert a coil voltage into slow-decay IN1/IN2 compare values. The input on the driven side is held high
 *        and the other input is low for duty * period counts (drive) and high for the rest (brake).
 *
 * \param voltage signed coil voltage (V)
 * \param supply_voltage VM (V)
 * \param period PWM period in counts (PER + 1); a compare value of `period` holds an output high
 * \param in1_cc receives the IN1 compare value
 * \param in2_cc receives the IN2 compare value
 */
void magnetorquer_compute_compare_values(float voltage, float supply_voltage, uint32_t period, uint32_t *in1_cc, uint32_t *in2_cc) {
    float magnitude = 0.0f;
    if (supply_voltage > 0.0f) {
        magnitude = magnetorquer_clamp_voltage(voltage, supply_voltage) / supply_voltage;
    }
    if (magnitude < 0.0f) {
        magnitude = -magnitude;
    }

    uint32_t drive_counts = (uint32_t)(magnitude * (float)period + 0.5f);
    if (drive_counts > period) {
        drive_counts = period;
    }

    if (voltage < 0.0f) {
        *in1_cc = period - drive_counts;
        *in2_cc = period;
    } else {
        *in1_cc = period;
        *in2_cc = period - drive_counts;
    }
}
