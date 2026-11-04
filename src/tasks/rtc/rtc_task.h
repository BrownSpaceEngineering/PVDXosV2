#ifndef RTC_TASK_H
#define RTC_TASK_H

// Includes
#include <atmel_start.h>
#include <driver_init.h>

#include "globals.h"
#include "tasks/watchdog/watchdog_task.h"

// Memory for the RTC task
#define RTC_TASK_STACK_SIZE 512 // Size of the stack in words (multiply by 4 to get bytes)
#define RTC_TASK_PERIOD_MS 1000 // calibrate_rtc() must close each window within 5 s of it reaching 100 s

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t rtc_task_stack[RTC_TASK_STACK_SIZE];
    StaticTask_t rtc_task_tcb;
} rtc_task_memory_t;

extern rtc_task_memory_t rtc_mem;

QueueHandle_t init_rtc(void);
void main_rtc(void *pvParameters);

#endif // RTC_TASK_H
