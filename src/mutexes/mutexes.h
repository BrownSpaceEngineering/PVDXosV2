#ifndef MUTEXES_H
#define MUTEXES_H

#include "globals.h"
#include "logging.h"
#include "rtos_start.h"
#include "task_list.h"

void lock_mutex(SemaphoreHandle_t mutex);
void unlock_mutex(SemaphoreHandle_t mutex);

// Shared SERCOM4 bus; first lock initializes both CS pins and enables the ASF-configured SPI.
// Non-recursive, task context only (or single-threaded startup before the scheduler).
// Do not call with the scheduler suspended. Hold through CS deassertion and response copying.
void lock_magnetometer_gyro_spi(void);
void unlock_magnetometer_gyro_spi(void);

// Shared SERCOM6 camera/PMB bus. Hold across the complete transaction, including repeated starts.
// Non-recursive, task context only (or single-threaded startup before the scheduler).
// Do not call with the scheduler suspended. Bus enablement must also use this lock.
void lock_camera_pmb_i2c(void);
void unlock_camera_pmb_i2c(void);

// Runtime-only, non-recursive. Camera init/capture owns this through CS cleanup;
// low-level camera calls inside those operations must not acquire it again.
#define TASK_SUSPEND_RESOURCE_TIMEOUT_MS 6000
void lock_camera_operation(void);
void unlock_camera_operation(void);

// Task-manager suspension barrier: camera operation -> shared SPI -> shared I2C.
// One total timeout, with partial acquisition unwound on failure. Never call while
// holding task_list_mutex, a bus lock, or with the scheduler suspended.
bool lock_task_suspend_resources(void);
void unlock_task_suspend_resources(void);

#endif // MUTEXES_H