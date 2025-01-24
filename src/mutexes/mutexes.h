#ifndef MUTEXES_H
#define MUTEXES_H

#include "rtos_start.h"
#include "logging.h"
#include "globals.h"
#include "task_list.h"

void lock_mutex(SemaphoreHandle_t mutex);
void unlock_mutex(SemaphoreHandle_t mutex);

#endif // MUTEXES_H