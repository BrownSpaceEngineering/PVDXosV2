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
