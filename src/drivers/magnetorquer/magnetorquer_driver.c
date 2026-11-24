/**
 * magnetorquer_driver.c
 *
 * Three DRV8837 H-bridges on TCC0 (X,Y) and TCC1 (Z). See magnetorquer_driver.h.
 */

#include "globals.h"

#include "magnetorquer_driver.h"

#include <math.h>

#include "os_delay.h"

// Per-axis mapping: which TCC, the two compare channels (IN1/IN2), and the nSLEEP pin.
typedef struct {
    Tcc *hw;
    uint8_t cc_in1;
    uint8_t cc_in2;
    uint32_t slp_pin;
} mtq_cfg_t;

static const mtq_cfg_t MTQ_CFG[MTQ_AXIS_COUNT] = {
    [MTQ_X] = {TCC0, 0, 1, MTQ_X_SLP},
    [MTQ_Y] = {TCC0, 2, 3, MTQ_Y_SLP},
    [MTQ_Z] = {TCC1, 0, 1, MTQ_Z_SLP},
};

// nSLEEP is active-low: high = driver awake, low = asleep.
#define MTQ_AWAKE_LEVEL  true
#define MTQ_ASLEEP_LEVEL false

// Cached TCC period (compare value for 100% duty) per axis, and last commanded duty.
static uint32_t mtq_period[MTQ_AXIS_COUNT];
static float mtq_duty[MTQ_AXIS_COUNT];

// Open mtq_quiet_begin() calls, and the tick when a coil last switched off. Both the ADCS task and the radio's task
// reach these, so they only change in critical sections.
static uint32_t mtq_quiet_count = 0;
static TickType_t mtq_off_tick = 0;

status_t init_magnetorquers(void) {
    // pwm_init() already ran in atmel_start_init (WAVEGEN=NPWM, PER configured). Enable the
    // timers so they start counting.
    pwm_enable(&MAGNETORQUER_1); // TCC0 (X, Y)
    pwm_enable(&MAGNETORQUER_2); // TCC1 (Z)

    for (uint8_t a = 0; a < MTQ_AXIS_COUNT; a++) {
        const mtq_cfg_t *c = &MTQ_CFG[a];
        mtq_period[a] = hri_tcc_get_PER_reg(c->hw, 0xFFFFFFFF);
        mtq_duty[a] = 0.0f;

        // Both inputs low (coast).
        hri_tcc_write_CCBUF_reg(c->hw, c->cc_in1, 0);
        hri_tcc_write_CCBUF_reg(c->hw, c->cc_in2, 0);

        // nSLEEP output, asleep.
        gpio_set_pin_function(c->slp_pin, GPIO_PIN_FUNCTION_OFF);
        gpio_set_pin_direction(c->slp_pin, GPIO_DIRECTION_OUT);
        gpio_set_pin_level(c->slp_pin, MTQ_ASLEEP_LEVEL);
    }

    return SUCCESS;
}

void mtq_set(mtq_axis_t axis, float duty) {
    if (axis >= MTQ_AXIS_COUNT) {
        return;
    }
    const mtq_cfg_t *c = &MTQ_CFG[axis];

    // Clamp to [-1, 1].
    if (duty > 1.0f) {
        duty = 1.0f;
    } else if (duty < -1.0f) {
        duty = -1.0f;
    }

    // A critical section, so that mtq_quiet_begin() can't run between the check and the register writes
    taskENTER_CRITICAL();
    if (mtq_quiet_count > 0) {
        duty = 0.0f;
    }
    // A coil switching off starts its current's decay (-Wfloat-equal rules out comparing duties with == 0)
    if (!(fabsf(duty) > 0.0f) && fabsf(mtq_duty[axis]) > 0.0f) {
        mtq_off_tick = xTaskGetTickCount();
    }
    mtq_duty[axis] = duty;

    float mag = fabsf(duty);
    uint32_t cc = (uint32_t)(mag * (float)mtq_period[axis] + 0.5f);
    if (cc > mtq_period[axis]) {
        cc = mtq_period[axis];
    }

    if (duty > 0.0f) {
        hri_tcc_write_CCBUF_reg(c->hw, c->cc_in1, cc);
        hri_tcc_write_CCBUF_reg(c->hw, c->cc_in2, 0);
    } else if (duty < 0.0f) {
        hri_tcc_write_CCBUF_reg(c->hw, c->cc_in1, 0);
        hri_tcc_write_CCBUF_reg(c->hw, c->cc_in2, cc);
    } else {
        hri_tcc_write_CCBUF_reg(c->hw, c->cc_in1, 0);
        hri_tcc_write_CCBUF_reg(c->hw, c->cc_in2, 0);
    }

    gpio_set_pin_level(c->slp_pin, mag > 0.0f ? MTQ_AWAKE_LEVEL : MTQ_ASLEEP_LEVEL);
    taskEXIT_CRITICAL();
}

void mtq_set_all(float x, float y, float z) {
    mtq_set(MTQ_X, x);
    mtq_set(MTQ_Y, y);
    mtq_set(MTQ_Z, z);
}

void mtq_stop(mtq_axis_t axis) {
    mtq_set(axis, 0.0f);
}

void mtq_stop_all(void) {
    mtq_stop(MTQ_X);
    mtq_stop(MTQ_Y);
    mtq_stop(MTQ_Z);
}

float mtq_get_duty(mtq_axis_t axis) {
    if (axis >= MTQ_AXIS_COUNT) {
        return 0.0f;
    }
    return mtq_duty[axis];
}

void mtq_quiet_begin(void) {
    taskENTER_CRITICAL();
    mtq_quiet_count++;
    taskEXIT_CRITICAL();
    mtq_stop_all();

    // The tick count can step just after a coil switches off, so one tick of the difference may not have passed
    const TickType_t decay_ticks = pdMS_TO_TICKS(MTQ_DECAY_MS) + 1;
    taskENTER_CRITICAL();
    const TickType_t since_off = xTaskGetTickCount() - mtq_off_tick;
    taskEXIT_CRITICAL();
    if (since_off < decay_ticks) {
        os_delay_ms((uint32_t)(decay_ticks - since_off) * portTICK_PERIOD_MS);
    }
}

void mtq_quiet_end(void) {
    taskENTER_CRITICAL();
    if (mtq_quiet_count > 0) {
        mtq_quiet_count--;
    }
    taskEXIT_CRITICAL();
}
