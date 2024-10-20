#ifndef UTILS_H
#define UTILS_H

#include "FreeRTOS.h"
#include "rtos_start.h"
#include "logging.h"

void lock_mutex(SemaphoreHandle_t mutex);
void unlock_mutex(SemaphoreHandle_t mutex);

#endif // UTILS_H