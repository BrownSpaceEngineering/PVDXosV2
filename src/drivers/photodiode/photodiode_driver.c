/**
 * photodiode_driver.c
 *
 * 18 photodiode channels across ADC0 and ADC1. See photodiode_driver.h.
 */

#include "globals.h"

#include "photodiode_driver.h"

// Photodiode index -> {ADC descriptor, AIN channel}.
typedef struct {
    struct adc_sync_descriptor *adc;
    uint8_t ain;
} phd_cfg_t;

static const phd_cfg_t PHD_CFG[PHD_COUNT] = {
    {&ADC_1, 10}, // PHD_0  PC00 ADC1_AIN10
    {&ADC_0, 1},  // PHD_1  PA03 ADC0_AIN1
    {&ADC_0, 2},  // PHD_2  PB08 ADC0_AIN2
    {&ADC_0, 3},  // PHD_3  PB09 ADC0_AIN3
    {&ADC_0, 4},  // PHD_4  PA04 ADC0_AIN4
    {&ADC_0, 5},  // PHD_5  PA05 ADC0_AIN5
    {&ADC_0, 6},  // PHD_6  PA06 ADC0_AIN6
    {&ADC_0, 7},  // PHD_7  PA07 ADC0_AIN7
    {&ADC_0, 8},  // PHD_8  PA08 ADC0_AIN8
    {&ADC_0, 9},  // PHD_9  PA09 ADC0_AIN9
    {&ADC_0, 10}, // PHD_10 PA10 ADC0_AIN10
    {&ADC_0, 11}, // PHD_11 PA11 ADC0_AIN11
    {&ADC_0, 12}, // PHD_12 PB00 ADC0_AIN12
    {&ADC_0, 13}, // PHD_13 PB01 ADC0_AIN13
    {&ADC_0, 14}, // PHD_14 PB02 ADC0_AIN14
    {&ADC_0, 15}, // PHD_15 PB03 ADC0_AIN15
    {&ADC_1, 4},  // PHD_16 PC02 ADC1_AIN4
    {&ADC_1, 5},  // PHD_17 PC03 ADC1_AIN5
};

status_t init_photodiodes(void) {
    // Ratiometric, 12-bit, on both ADCs. (ADC1 config is shared with the thermistor driver;
    // identical settings, so this is idempotent.)
    struct adc_sync_descriptor *const adcs[2] = {&ADC_0, &ADC_1};
    for (uint8_t i = 0; i < 2; i++) {
        adc_sync_set_reference(adcs[i], ADC_REFCTRL_REFSEL_INTVCC1_Val);
        adc_sync_set_resolution(adcs[i], ADC_CTRLB_RESSEL_12BIT_Val);
        adc_sync_enable_channel(adcs[i], 0);
    }
    return SUCCESS;
}

status_t phd_read_raw(uint8_t index, uint16_t *code) {
    if (index >= PHD_COUNT || code == NULL) {
        return ERROR_BAD_TARGET;
    }
    const phd_cfg_t *c = &PHD_CFG[index];

    if (adc_sync_set_inputs(c->adc, c->ain, ADC_INPUTCTRL_MUXNEG_GND_Val, 0) != ERR_NONE) {
        warning("photodiode: set_inputs failed (idx=%u)\n", index);
        return ERROR_READ_FAILED;
    }

    uint8_t buf[2];
    // Settling conversion after switching the mux (discarded).
    if (adc_sync_read_channel(c->adc, 0, buf, sizeof(buf)) < 0) {
        warning("photodiode: ADC read failed (idx=%u)\n", index);
        return ERROR_READ_FAILED;
    }

    uint32_t acc = 0;
    for (uint8_t i = 0; i < PHD_ADC_AVG_SAMPLES; i++) {
        if (adc_sync_read_channel(c->adc, 0, buf, sizeof(buf)) < 0) {
            warning("photodiode: ADC read failed (idx=%u)\n", index);
            return ERROR_READ_FAILED;
        }
        acc += (uint32_t)buf[0] | ((uint32_t)buf[1] << 8);
    }

    *code = (uint16_t)(acc / PHD_ADC_AVG_SAMPLES);
    return SUCCESS;
}

status_t phd_read_voltage(uint8_t index, float *volts) {
    if (volts == NULL) {
        return ERROR_BAD_TARGET;
    }
    uint16_t code = 0;
    ret_err_status(phd_read_raw(index, &code), "photodiode: raw read failed");
    *volts = (float)code / PHD_ADC_FS * PHD_VREF_V;
    return SUCCESS;
}

status_t phd_read_all(uint16_t codes[PHD_COUNT]) {
    if (codes == NULL) {
        return ERROR_BAD_TARGET;
    }
    status_t worst = SUCCESS;
    for (uint8_t i = 0; i < PHD_COUNT; i++) {
        codes[i] = 0;
        status_t rc = phd_read_raw(i, &codes[i]);
        if (rc != SUCCESS) {
            worst = rc;
        }
    }
    return worst;
}
