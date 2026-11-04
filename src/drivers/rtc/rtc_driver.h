#ifndef RTC_DRIVER_H
#define RTC_DRIVER_H

#include "globals.h"

// The RTC counts the 32.768 kHz OSCULP32K divided by 1024, so the 32-bit counter wraps after about 4.25 years. The
// oscillator is only accurate to a few percent, so the real tick rate is measured against the UHF radio (see
// calibrate_rtc()) and this nominal rate is only used until the first measurement.
#define RTC_TICKS_PER_SECOND 32

// Data structure to hold RTC values
typedef struct {
    uint32_t rtc_count;
    uint32_t seconds_count;
    uint64_t milliseconds_count;
} rtc_data_t;

// Function declarations
status_t init_rtc_hardware(void);
status_t get_rtc_values(rtc_data_t *data);
void calibrate_rtc(void);

#endif // RTC_DRIVER_H
