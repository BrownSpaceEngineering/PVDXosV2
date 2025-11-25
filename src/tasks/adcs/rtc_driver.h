#ifndef RTC_DRIVER_H
#define RTC_DRIVER_H

#include "globals.h"
#include "rtos_start.h"
#include "watchdog_task.h"
#include "atmel_start.h"
#include "driver_init.h"

// Data structure to hold RTC values
typedef struct {
    uint32_t rtc_count;
    uint32_t seconds_count;
    uint32_t microseconds_count;
} rtc_data_t;

// Function declarations
status_t init_rtc_hardware(void);
status_t get_rtc_values(rtc_data_t *data);

#endif // RTC_DRIVER_H