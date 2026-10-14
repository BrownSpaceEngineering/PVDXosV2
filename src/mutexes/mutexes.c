/**
 * mutexes.c
 *
 * Helper functions for locking and unlocking FreeRTOS mutexes.
 *
 * Created: September 29, 2024
 * Authors: Tanish Makadia, Simon Juknelis, Defne Doken, Aidan Wang, Yi Liu
 */

#include "mutexes.h"

#include "atmel_start.h"
#include "task_list.h"

static StaticSemaphore_t magnetometer_gyro_spi_mutex_buffer;
static SemaphoreHandle_t magnetometer_gyro_spi_mutex;
static bool magnetometer_gyro_spi_initialized;

void lock_magnetometer_gyro_spi(void) {
    // Startup checks run before any task can contend; there is no mutex owner then.
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        // Neither driver requires an external init call. Publish exactly one static mutex,
        // even when both tasks reach their first transaction at the same time.
        taskENTER_CRITICAL();
        if (magnetometer_gyro_spi_mutex == NULL) {
            magnetometer_gyro_spi_mutex = xSemaphoreCreateMutexStatic(&magnetometer_gyro_spi_mutex_buffer);
        }
        taskEXIT_CRITICAL();
        if (magnetometer_gyro_spi_mutex == NULL) {
            fatal("Failed to create magnetometer/gyro SPI mutex\n");
        }
        lock_mutex(magnetometer_gyro_spi_mutex);
    }

    // Run once under the bus lock, not independently in each sensor's init path.
    // Preserve main's mode 0 / 8-bit / 50 kHz ASF configuration.
    if (!magnetometer_gyro_spi_initialized) {
        gpio_set_pin_level(MAGNETOMETER_CS, true);
        gpio_set_pin_level(GYRO_CS, true);
        gpio_set_pin_function(MAGNETOMETER_CS, GPIO_PIN_FUNCTION_OFF);
        gpio_set_pin_function(GYRO_CS, GPIO_PIN_FUNCTION_OFF);
        gpio_set_pin_direction(MAGNETOMETER_CS, GPIO_DIRECTION_OUT);
        gpio_set_pin_direction(GYRO_CS, GPIO_DIRECTION_OUT);
        spi_m_sync_enable(&SPI_MAGNETOMETER_GYRO);
        magnetometer_gyro_spi_initialized = true;
    }
}

void unlock_magnetometer_gyro_spi(void) {
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        unlock_mutex(magnetometer_gyro_spi_mutex);
    }
}

static StaticSemaphore_t camera_pmb_i2c_mutex_buffer;
static SemaphoreHandle_t camera_pmb_i2c_mutex;

void lock_camera_pmb_i2c(void) {
    // Startup checks are single-threaded and cannot take a task-owned mutex.
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        taskENTER_CRITICAL();
        if (camera_pmb_i2c_mutex == NULL) {
            camera_pmb_i2c_mutex = xSemaphoreCreateMutexStatic(&camera_pmb_i2c_mutex_buffer);
        }
        taskEXIT_CRITICAL();
        if (camera_pmb_i2c_mutex == NULL) {
            fatal("Failed to create camera/PMB I2C mutex\n");
        }
        lock_mutex(camera_pmb_i2c_mutex);
    }
}

void unlock_camera_pmb_i2c(void) {
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        unlock_mutex(camera_pmb_i2c_mutex);
    }
}

static StaticSemaphore_t camera_operation_mutex_buffer;
static SemaphoreHandle_t camera_operation_mutex;

static void init_suspend_mutexes(void) {
    taskENTER_CRITICAL();
    if (camera_operation_mutex == NULL) {
        camera_operation_mutex = xSemaphoreCreateMutexStatic(&camera_operation_mutex_buffer);
    }
    if (magnetometer_gyro_spi_mutex == NULL) {
        magnetometer_gyro_spi_mutex = xSemaphoreCreateMutexStatic(&magnetometer_gyro_spi_mutex_buffer);
    }
    if (camera_pmb_i2c_mutex == NULL) {
        camera_pmb_i2c_mutex = xSemaphoreCreateMutexStatic(&camera_pmb_i2c_mutex_buffer);
    }
    taskEXIT_CRITICAL();
    if (camera_operation_mutex == NULL || magnetometer_gyro_spi_mutex == NULL || camera_pmb_i2c_mutex == NULL) {
        fatal("Failed to create task suspension mutexes\n");
    }
}

void lock_camera_operation(void) {
    init_suspend_mutexes();
    if (xSemaphoreTake(camera_operation_mutex, pdMS_TO_TICKS(TASK_SUSPEND_RESOURCE_TIMEOUT_MS)) != pdTRUE) {
        fatal("Failed to lock camera operation\n");
    }
}

void unlock_camera_operation(void) {
    unlock_mutex(camera_operation_mutex);
}

static bool take_suspend_mutex(SemaphoreHandle_t mutex, TickType_t start) {
    const TickType_t elapsed = xTaskGetTickCount() - start;
    const TickType_t budget = pdMS_TO_TICKS(TASK_SUSPEND_RESOURCE_TIMEOUT_MS);
    return xSemaphoreTake(mutex, elapsed < budget ? budget - elapsed : 0) == pdTRUE;
}

bool lock_task_suspend_resources(void) {
    init_suspend_mutexes();
    const TickType_t start = xTaskGetTickCount();
    // Camera init takes I2C internally: its operation lock must always come first.
    if (!take_suspend_mutex(camera_operation_mutex, start)) {
        return false;
    }
    if (!take_suspend_mutex(magnetometer_gyro_spi_mutex, start)) {
        unlock_camera_operation();
        return false;
    }
    if (!take_suspend_mutex(camera_pmb_i2c_mutex, start)) {
        unlock_magnetometer_gyro_spi();
        unlock_camera_operation();
        return false;
    }
    return true;
}

void unlock_task_suspend_resources(void) {
    unlock_camera_pmb_i2c();
    unlock_magnetometer_gyro_spi();
    unlock_camera_operation();
}

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
