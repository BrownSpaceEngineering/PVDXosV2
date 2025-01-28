/**
 * mutexes.c
 * 
 * Helper functions for locking and unlocking FreeRTOS mutexes by repeatedly polling them.
 * 
 * Created: September 29, 2024
 * Author: Tanish Makadia
 */

#include "mutexes.h"

// How long to wait before trying to lock a mutex again (in ms)
#define POLLING_DELAY_MS 5

// Maximum number of tries before returning a timeout error
#define MAX_TRIES 1000u

// Polls a mutex until it is available, then locks it
void lock_mutex(SemaphoreHandle_t mutex) {
    uint16_t num_tries = 0;

    while (xSemaphoreTake(mutex, (TickType_t)0) == pdFALSE) {
        // Mutex is not available yet. Delay and try again.
        vTaskDelay(pdMS_TO_TICKS(POLLING_DELAY_MS));

        num_tries++;
        if (num_tries > MAX_TRIES) {
            pvdx_task_t* calling_task = get_task(xTaskGetCurrentTaskHandle());
            fatal("%s task failed to lock mutex after %d tries\n", calling_task->name, MAX_TRIES);
        }
    }
}

// Unlocks a mutex that was previously locked
void unlock_mutex(SemaphoreHandle_t mutex) {
    if (xSemaphoreGive(mutex) != pdTRUE) {
        pvdx_task_t* calling_task = get_task(xTaskGetCurrentTaskHandle());
        fatal("%s task failed to unlock mutex\n", calling_task->name);
    }
}