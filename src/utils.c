#include <utils.h>

// How long to wait before trying to lock a mutex again (in ms)
POLLING_DELAY_MS = 5;

void lock_mutex(SemaphoreHandle_t mutex) {
    while (xSemaphoreTake(mutex, (TickType_t)0) == pdFALSE) {
        // Mutex is not available yet. Delay to avoid busy-waiting.
        vTaskDelay(pdMS_TO_TICKS(POLLING_DELAY_MS));
    }
    
    // We have locked the mutex; continue as normal
}

void unlock_mutex(SemaphoreHandle_t mutex) {
    bool success = xSemaphoreGive(mutex);

    if (!success) {
        fatal("Failed to unlock a mutex");
    }
}