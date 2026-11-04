#ifndef RTC_DRIVER_H
#define RTC_DRIVER_H

#include "globals.h"

// Data structure to hold RTC values
typedef struct {
    uint32_t rtc_count;
    uint32_t seconds_count;      // wraps along with rtc_count, about every 36 hours
    uint32_t microseconds_count; // wraps about every 72 minutes
} rtc_data_t;

// Function declarations
status_t init_rtc_hardware(void);
status_t get_rtc_values(rtc_data_t *data);

#endif // RTC_DRIVER_H
