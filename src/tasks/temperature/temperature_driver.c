#include "temperature_driver.h"

#include "logging.h"

#include <hri_supc_d51.h>
#include <math.h>
#include <string.h>

#define TEMP_SENSOR_ADC_INSTANCE ADC1

#define TEMP_SENSOR_DEFAULT_PRESCALER ADC_CTRLA_PRESCALER_DIV32
#define TEMP_SENSOR_DEFAULT_SAMPLENUM ADC_AVGCTRL_SAMPLENUM_32
#define TEMP_SENSOR_DEFAULT_ADJRES 5
#define TEMP_SENSOR_DEFAULT_SAMPLEN 5

typedef struct {
    bool loaded;
    float room_temp_c;
    float hot_temp_c;
    uint16_t room_ptat;
    uint16_t hot_ptat;
    uint16_t room_ctat;
    uint16_t hot_ctat;
    float ptat_slope;
    float ptat_intercept;
    float ctat_slope;
    float ctat_intercept;
} temp_calibration_t;

static temp_calibration_t g_calibration = {0};
static bool g_temp_sensor_initialized = false;

static inline void wait_sync(uint32_t mask) {
    while (TEMP_SENSOR_ADC_INSTANCE->SYNCBUSY.reg & mask) {
    }
}

static inline float decode_temperature(uint8_t integer_part, uint8_t fractional_part) {
    return (float)integer_part + ((float)fractional_part / 16.0f);
}

static status_t load_calibration(temp_calibration_t *cal) {
    if (!cal) {
        return ERROR_SANITY_CHECK_FAILED;
    }

    memset(cal, 0, sizeof(*cal));

    const uint32_t temp_log0 = *((uint32_t *)NVMCTRL_TEMP_LOG);
    const uint32_t temp_log1 = *((uint32_t *)(NVMCTRL_TEMP_LOG + 4U));
    const uint32_t temp_log2 = *((uint32_t *)(NVMCTRL_TEMP_LOG + 8U));

    const uint8_t room_temp_int = (temp_log0 & FUSES_ROOM_TEMP_VAL_INT_Msk) >> FUSES_ROOM_TEMP_VAL_INT_Pos;
    const uint8_t room_temp_dec = (temp_log0 & FUSES_ROOM_TEMP_VAL_DEC_Msk) >> FUSES_ROOM_TEMP_VAL_DEC_Pos;
    const uint8_t hot_temp_int = (temp_log0 & FUSES_HOT_TEMP_VAL_INT_Msk) >> FUSES_HOT_TEMP_VAL_INT_Pos;
    const uint8_t hot_temp_dec = (temp_log0 & FUSES_HOT_TEMP_VAL_DEC_Msk) >> FUSES_HOT_TEMP_VAL_DEC_Pos;

    const uint16_t room_ptat_raw = (temp_log1 & FUSES_ROOM_ADC_VAL_PTAT_Msk) >> FUSES_ROOM_ADC_VAL_PTAT_Pos;
    const uint16_t hot_ptat_raw = (temp_log1 & FUSES_HOT_ADC_VAL_PTAT_Msk) >> FUSES_HOT_ADC_VAL_PTAT_Pos;

    const uint16_t room_ctat_raw = (temp_log2 & FUSES_ROOM_ADC_VAL_CTAT_Msk) >> FUSES_ROOM_ADC_VAL_CTAT_Pos;
    const uint16_t hot_ctat_raw = (temp_log2 & FUSES_HOT_ADC_VAL_CTAT_Msk) >> FUSES_HOT_ADC_VAL_CTAT_Pos;

    cal->room_temp_c = decode_temperature(room_temp_int, room_temp_dec);
    cal->hot_temp_c = decode_temperature(hot_temp_int, hot_temp_dec);

    const float delta_temp = cal->hot_temp_c - cal->room_temp_c;
    if (fabsf(delta_temp) < 0.01f) {
        warning("temperature: calibration delta temperature too small (%.2f)", delta_temp);
        return ERROR_SANITY_CHECK_FAILED;
    }

    cal->room_ptat = room_ptat_raw;
    cal->hot_ptat = hot_ptat_raw;
    cal->room_ctat = room_ctat_raw;
    cal->hot_ctat = hot_ctat_raw;

    cal->ptat_slope = ((float)hot_ptat_raw - (float)room_ptat_raw) / delta_temp;
    cal->ctat_slope = ((float)hot_ctat_raw - (float)room_ctat_raw) / delta_temp;

    if (fabsf(cal->ptat_slope) < 1e-6f || fabsf(cal->ctat_slope) < 1e-6f) {
        warning("temperature: calibration slope too small (ptat %.6f, ctat %.6f)", cal->ptat_slope, cal->ctat_slope);
        return ERROR_SANITY_CHECK_FAILED;
    }

    cal->ptat_intercept = (float)room_ptat_raw - (cal->ptat_slope * cal->room_temp_c);
    cal->ctat_intercept = (float)room_ctat_raw - (cal->ctat_slope * cal->room_temp_c);
    cal->loaded = true;

    return SUCCESS;
}

static inline uint16_t read_adc_result(void) {
    while (!(TEMP_SENSOR_ADC_INSTANCE->INTFLAG.reg & ADC_INTFLAG_RESRDY)) {
    }
    const uint16_t result = TEMP_SENSOR_ADC_INSTANCE->RESULT.reg;
    TEMP_SENSOR_ADC_INSTANCE->INTFLAG.reg = ADC_INTFLAG_RESRDY | ADC_INTFLAG_OVERRUN;
    return result;
}

static uint16_t sample_internal_channel(uint8_t muxpos) {
    wait_sync(ADC_SYNCBUSY_INPUTCTRL);
    TEMP_SENSOR_ADC_INSTANCE->INPUTCTRL.reg =
        ADC_INPUTCTRL_MUXPOS(muxpos) | ADC_INPUTCTRL_MUXNEG_GND;
    wait_sync(ADC_SYNCBUSY_INPUTCTRL);

    TEMP_SENSOR_ADC_INSTANCE->SWTRIG.reg |= ADC_SWTRIG_START;
    wait_sync(ADC_SYNCBUSY_SWTRIG);
    return read_adc_result();
}

static status_t ensure_adc_ready(void) {
    if (!(TEMP_SENSOR_ADC_INSTANCE->CTRLA.reg & ADC_CTRLA_ENABLE)) {
        TEMP_SENSOR_ADC_INSTANCE->CTRLA.reg |= ADC_CTRLA_ENABLE;
        wait_sync(ADC_SYNCBUSY_ENABLE);
    }
    return SUCCESS;
}

status_t temp_sensor_init(void) {
    if (g_temp_sensor_initialized) {
        return SUCCESS;
    }

    debug("temperature: initializing internal temperature sensor driver\n");

    status_t status = load_calibration(&g_calibration);
    if (status != SUCCESS) {
        return status;
    }

    hri_supc_set_VREF_TSEN_bit(SUPC);

    if (TEMP_SENSOR_ADC_INSTANCE->CTRLA.reg & ADC_CTRLA_ENABLE) {
        TEMP_SENSOR_ADC_INSTANCE->CTRLA.reg &= ~ADC_CTRLA_ENABLE;
        wait_sync(ADC_SYNCBUSY_ENABLE);
    }

    TEMP_SENSOR_ADC_INSTANCE->CTRLA.reg &= ~ADC_CTRLA_PRESCALER_Msk;
    TEMP_SENSOR_ADC_INSTANCE->CTRLA.reg |= TEMP_SENSOR_DEFAULT_PRESCALER;

    wait_sync(ADC_SYNCBUSY_REFCTRL);
    TEMP_SENSOR_ADC_INSTANCE->REFCTRL.reg = ADC_REFCTRL_REFSEL_INTREF;

    wait_sync(ADC_SYNCBUSY_AVGCTRL);
    TEMP_SENSOR_ADC_INSTANCE->AVGCTRL.reg =
        TEMP_SENSOR_DEFAULT_SAMPLENUM | ADC_AVGCTRL_ADJRES(TEMP_SENSOR_DEFAULT_ADJRES);

    wait_sync(ADC_SYNCBUSY_SAMPCTRL);
    TEMP_SENSOR_ADC_INSTANCE->SAMPCTRL.reg = ADC_SAMPCTRL_SAMPLEN(TEMP_SENSOR_DEFAULT_SAMPLEN);

    wait_sync(ADC_SYNCBUSY_CTRLB);
    TEMP_SENSOR_ADC_INSTANCE->CTRLB.reg = ADC_CTRLB_RESSEL_12BIT;

    TEMP_SENSOR_ADC_INSTANCE->INTFLAG.reg = ADC_INTFLAG_MASK;

    wait_sync(ADC_SYNCBUSY_ENABLE);
    TEMP_SENSOR_ADC_INSTANCE->CTRLA.reg |= ADC_CTRLA_ENABLE;
    wait_sync(ADC_SYNCBUSY_ENABLE);

    g_temp_sensor_initialized = true;
    info("temperature: driver initialized (room %.2fC, hot %.2fC)\n",
         g_calibration.room_temp_c, g_calibration.hot_temp_c);
    return SUCCESS;
}

status_t temp_sensor_sample(temp_sensor_sample_t *sample) {
    if (!sample) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    if (!g_temp_sensor_initialized || !g_calibration.loaded) {
        return ERROR_NOT_READY;
    }

    taskENTER_CRITICAL();
    ensure_adc_ready();
    const uint16_t ptat = sample_internal_channel(ADC1_PTAT);
    const uint16_t ctat = sample_internal_channel(ADC1_CTAT);
    taskEXIT_CRITICAL();

    const float ptat_norm = (float)ptat;
    const float ctat_norm = (float)ctat;

    const float temp_ptat =
        (ptat_norm - g_calibration.ptat_intercept) / g_calibration.ptat_slope;
    const float temp_ctat =
        (ctat_norm - g_calibration.ctat_intercept) / g_calibration.ctat_slope;

    sample->ptat_raw = ptat;
    sample->ctat_raw = ctat;
    sample->temperature_ptat_c = temp_ptat;
    sample->temperature_ctat_c = temp_ctat;
    sample->temperature_c = (temp_ptat + temp_ctat) * 0.5f;

    return SUCCESS;
}

status_t temp_sensor_get_celsius(float *temperature_c) {
    if (!temperature_c) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    temp_sensor_sample_t sample = {0};
    status_t status = temp_sensor_sample(&sample);
    if (status != SUCCESS) {
        return status;
    }

    *temperature_c = sample.temperature_c;
    return SUCCESS;
}

