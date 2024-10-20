#include "globals.h"
#include "mutexes.h"

// How long to wait before trying to lock a mutex again (in ms)
#define POLLING_DELAY_MS 5

// Maximum number of tries before returning a timeout error
#define MAX_TRIES 1000u

void lock_mutex(SemaphoreHandle_t mutex) {
    uint16_t num_tries = 0;
    // Check whether we have the green light to proceed
    while (xSemaphoreTake(mutex, (TickType_t)0) == pdFALSE) {
        // Mutex is not available yet. Delay to avoid busy-waiting.
        vTaskDelay(pdMS_TO_TICKS(POLLING_DELAY_MS));
        num_tries++;
        if (num_tries > MAX_TRIES) {
            fatal("Failed to lock mutex after %d tries", MAX_TRIES);
        }
    }
}

void unlock_mutex(SemaphoreHandle_t mutex) {
    bool success = xSemaphoreGive(mutex);
    // Unlock the mutex after data has been modified

    if (!success) {
        fatal("Failed to unlock a mutex");
    }
}