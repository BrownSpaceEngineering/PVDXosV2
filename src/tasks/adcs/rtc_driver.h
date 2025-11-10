#ifndef RTC_DRIVER_H
#define RTC_DRIVER_H

#include "globals.h"
#include "rtos_start.h"
#include "watchdog_task.h"
#include "atmel_start.h"
#include "driver_init.h"

// Function declarations
status_t init_rtc_hardware(void);
uint32_t get_rtc_count(void);

#endif // RTC_DRIVER_H