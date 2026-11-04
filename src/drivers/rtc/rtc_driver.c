/**
 * rtc_driver.c
 *
 * Hardware driver for RTC timer used in ADCS algorithms. The RTC runs from the internal OSCULP32K, which is only
 * accurate to a few percent, so calibrate_rtc() measures each ~100 s of RTC ticks against the UHF radio's timestamp
 * counter (32 MHz from the radio's 26 MHz crystal). Time between radio samples comes from the radio, and the RTC carries
 * it on at the last measured rate whenever the radio is unavailable.
 *
 * Created: November 9, 2025
 * Modified: November 4, 2026
 * Authors: Alexander Thaep
 */

#include "rtc_driver.h"

#include "atmel_start.h"
#include "drivers/at86rf215/at86rf215.h"
#include "drivers/at86rf215/regs.h"
#include "logging.h"

#define RADIO_COUNTER_TICKS_PER_US 32.0 // BBCn_CNT runs at 32 MHz (AT86RF215 datasheet 6.18)
#define RADIO_CNTC_EN 0x01              // BBCn_CNTC: free running, no capture or reset on RX/TX

// Calibration windows are 100 to 105 nominal RTC seconds. Even at the slowest OSCULP32K rate (26.00 kHz, 25.4 ticks/s
// after the prescaler) that is under the 134 s it takes the 32-bit radio counter to wrap.
#define CAL_WINDOW_MIN_TICKS (100 * RTC_TICKS_PER_SECOND)
#define CAL_WINDOW_MAX_TICKS (105 * RTC_TICKS_PER_SECOND)

// OSCULP32K runs between 26.00 and 40.92 kHz (SAMD51 datasheet), i.e. 25.0 to 39.4 ms per tick after the prescaler
#define MIN_US_PER_TICK 25000.0
#define MAX_US_PER_TICK 39400.0

// Bounds the wait for the next RTC tick (31.25 ms nominal) in case the RTC has stopped
#define TICK_EDGE_TIMEOUT_US 50000u
#define TICK_EDGE_POLL_US 10u

static const void *rtc_hw;

// Only used for register access, so it is never set up with at86rf215_init() (which would reset the radio)
static struct at86rf215 radio;
static bool radio_counter_on = false;

// The time is base_us at RTC count base_count, plus us_per_tick for every tick since
static uint32_t base_count;
static uint64_t base_us;
static double us_per_tick;

// Start of the calibration window in progress
static bool window_open = false;
static uint32_t window_rtc_count;
static uint32_t window_radio_count;
static uint64_t window_us;

static uint32_t read_rtc_count(void) {
    return hri_rtcmode0_get_COUNT_reg(rtc_hw, 0xFFFFFFFFu);
}

// Must be called in a critical section, since calibrate_rtc() moves the base
static uint64_t count_to_us(uint32_t count) {
    return base_us + (uint64_t)((double)(count - base_count) * us_per_tick);
}

// Turns on the radio's timestamp counter if it is off (any radio reset turns it off)
static bool enable_radio_counter(void) {
    uint8_t part_number = 0;
    if (at86rf215_reg_read_8(&radio, &part_number, REG_RF_PN) != AT86RF215_OK ||
        (part_number != AT86RF215 && part_number != AT86RF215IQ && part_number != AT86RF215M)) {
        return false;
    }
    uint8_t cntc = 0;
    if (at86rf215_reg_read_8(&radio, &cntc, REG_BBC0_CNTC) != AT86RF215_OK) {
        return false;
    }
    if (cntc == RADIO_CNTC_EN) {
        return true;
    }
    return at86rf215_reg_write_8(&radio, RADIO_CNTC_EN, REG_BBC0_CNTC) == AT86RF215_OK;
}

// BBC0_CNT0..3 must be read in one SPI block access to be consistent. at86rf215_reg_read_32() puts the first register
// (CNT0, the least significant byte) in the top byte, so the result is byte-swapped.
static bool read_radio_counter(uint32_t *count) {
    uint32_t first_byte_high = 0;
    if (at86rf215_reg_read_32(&radio, &first_byte_high, REG_BBC0_CNT0) != AT86RF215_OK) {
        return false;
    }
    *count = __builtin_bswap32(first_byte_high);
    return true;
}

// Waits for the RTC to tick and reads the radio counter straight after, so both counts mark the same instant to within
// the SPI read rather than a whole RTC tick. The scheduler is suspended so no task runs in between.
static bool sample_tick_edge(uint32_t *rtc_count, uint32_t *radio_count) {
    vTaskSuspendAll();
    const uint32_t start = read_rtc_count();
    uint32_t now = start;
    for (uint32_t waited_us = 0; now == start && waited_us < TICK_EDGE_TIMEOUT_US; waited_us += TICK_EDGE_POLL_US) {
        delay_us(TICK_EDGE_POLL_US);
        now = read_rtc_count();
    }
    const bool ok = now != start && read_radio_counter(radio_count);
    xTaskResumeAll();
    *rtc_count = now;
    return ok;
}

/**
 * \fn init_rtc_hardware
 *
 * \brief Initialize RTC timer hardware, and start the UHF radio's timestamp counter used to calibrate it
 *
 * \returns status_t SUCCESS if initialization was successful
 */
status_t init_rtc_hardware(void) {
    rtc_hw = (&TIMER_0.device)->hw;
    hri_rtcmode0_clear_CTRLA_ENABLE_bit(rtc_hw);
    hri_rtcmode0_clear_CTRLA_MATCHCLR_bit(rtc_hw);
    hri_rtcmode0_write_CTRLA_PRESCALER_bf(rtc_hw, RTC_MODE0_CTRLA_PRESCALER_DIV1024_Val);
    hri_rtcmode0_write_COUNT_reg(rtc_hw, 0);
    hri_rtcmode0_wait_for_sync(rtc_hw, RTC_MODE0_SYNCBUSY_COUNT);
    hri_rtcmode0_set_CTRLA_ENABLE_bit(rtc_hw);

    base_count = 0;
    base_us = 0;
    us_per_tick = 1e6 / RTC_TICKS_PER_SECOND;
    window_open = false;

    spi_m_sync_enable(&SPI_UHF);
    radio_counter_on = enable_radio_counter();
    if (!radio_counter_on) {
        warning("rtc: UHF radio counter unavailable, RTC runs uncalibrated until it answers\n");
    }
    return SUCCESS;
}

/**
 * \fn calibrate_rtc
 *
 * \brief Measure the RTC against the UHF radio's timestamp counter. The RTC task calls it every second: it opens a
 * window, and once the window is 100 to 105 RTC seconds long it closes it, corrects the time over the window to the
 * radio's measurement, takes the measured tick rate, and opens the next window at the same instant. A window that runs
 * too long (the radio counter may have wrapped) or gives an impossible rate (the radio was reset) is dropped.
 */
void calibrate_rtc(void) {
    if (!rtc_hw) {
        return;
    }

    if (!window_open) {
        if (!radio_counter_on) {
            radio_counter_on = enable_radio_counter();
        }
        if (!radio_counter_on) {
            return;
        }
        if (sample_tick_edge(&window_rtc_count, &window_radio_count)) {
            taskENTER_CRITICAL();
            window_us = count_to_us(window_rtc_count);
            taskEXIT_CRITICAL();
            window_open = true;
        }
        return;
    }

    const uint32_t elapsed_ticks = read_rtc_count() - window_rtc_count;
    if (elapsed_ticks < CAL_WINDOW_MIN_TICKS) {
        return;
    }
    uint32_t rtc_count = 0;
    uint32_t radio_count = 0;
    if (elapsed_ticks >= CAL_WINDOW_MAX_TICKS || !sample_tick_edge(&rtc_count, &radio_count)) {
        window_open = false;
        return;
    }

    const double window_real_us = (double)(radio_count - window_radio_count) / RADIO_COUNTER_TICKS_PER_US;
    const double measured_us_per_tick = window_real_us / (double)(rtc_count - window_rtc_count);
    if (measured_us_per_tick < MIN_US_PER_TICK || measured_us_per_tick > MAX_US_PER_TICK) {
        warning("rtc: dropped calibration window (%u us per tick), checking the radio counter again\n",
                (unsigned int)measured_us_per_tick);
        window_open = false;
        radio_counter_on = false;
        return;
    }

    taskENTER_CRITICAL();
    base_us = window_us + (uint64_t)window_real_us;
    base_count = rtc_count;
    us_per_tick = measured_us_per_tick;
    taskEXIT_CRITICAL();

    window_rtc_count = rtc_count;
    window_radio_count = radio_count;
    window_us = base_us;
}

/**
 * \fn get_rtc_values
 *
 * \brief Get RTC raw count, and the calibrated milliseconds and seconds since init_rtc_hardware()
 *
 * \param data pointer to rtc_data_t structure to fill
 *
 * \returns status_t SUCCESS if reading was successful
 */
status_t get_rtc_values(rtc_data_t *data) {
    if (!rtc_hw) {
        warning("Attempting to get RTC count before initializing RTC");
        return ERROR_NOT_READY;
    }
    taskENTER_CRITICAL();
    data->rtc_count = read_rtc_count();
    const uint64_t us = count_to_us(data->rtc_count);
    taskEXIT_CRITICAL();
    data->milliseconds_count = us / 1000;
    data->seconds_count = (uint32_t)(us / 1000000);
    return SUCCESS;
}
