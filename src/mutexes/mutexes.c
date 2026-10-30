/**
 * mutexes.c
 *
 * Helper functions for locking and unlocking FreeRTOS mutexes.
 *
 * Created: September 29, 2024
 * Authors: Tanish Makadia, Simon Juknelis, Defne Doken, Aidan Wang, Yi Liu
 */

#include "mutexes.h"

#include "task_list.h"

// Polls a mutex until it is available, then locks it
void lock_mutex(SemaphoreHandle_t mutex) {
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        fatal("%s task failed to lock mutex\n", get_current_task()->name);
    }
}

// Unlocks a mutex that was previously locked
void unlock_mutex(SemaphoreHandle_t mutex) {
    if (xSemaphoreGive(mutex) != pdTRUE) {
        fatal("%s task failed to unlock mutex\n", get_current_task()->name);
    }
}

static SemaphoreHandle_t adc1_mutex;
static StaticSemaphore_t adc1_mutex_buffer;

void init_adc1_mutex(void) {
    adc1_mutex = xSemaphoreCreateMutexStatic(&adc1_mutex_buffer);
    if (adc1_mutex == NULL) {
        fatal("Failed to create ADC1 mutex\n");
    }
}

void lock_adc1(void) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        lock_mutex(adc1_mutex);
    }
}

void unlock_adc1(void) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        unlock_mutex(adc1_mutex);
    }
}
