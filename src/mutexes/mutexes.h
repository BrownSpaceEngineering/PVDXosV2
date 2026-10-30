#ifndef MUTEXES_H
#define MUTEXES_H

#include "rtos_start.h"
#include "logging.h"
#include "globals.h"
#include "task_list.h"

void lock_mutex(SemaphoreHandle_t mutex);
void unlock_mutex(SemaphoreHandle_t mutex);

// ADC_1 is shared by the photodiode and thermistor drivers. A read selects a channel and then converts, so another
// task must not switch the channel in between. These do nothing until the scheduler is running, since the startup
// device checks read the ADCs before then and nothing can preempt them.
void init_adc1_mutex(void);
void lock_adc1(void);
void unlock_adc1(void);

#endif // MUTEXES_H