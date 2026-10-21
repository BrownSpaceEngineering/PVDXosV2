#ifndef MUTEXES_H
#define MUTEXES_H

#include "rtos_start.h"
#include "logging.h"
#include "globals.h"
#include "task_list.h"

// Guards the shared I2C_CAMERA bus (Arducam OV2640 + fuel-gauge TCA9546A mux/MAX17205s).
// Hold it for every transaction on that bus, and across any fuel-gauge mux-select -> read -> close sequence.
extern SemaphoreHandle_t i2c_camera_bus_mutex;
extern StaticSemaphore_t i2c_camera_bus_mutex_buffer;

void lock_mutex(SemaphoreHandle_t mutex);
void unlock_mutex(SemaphoreHandle_t mutex);

#endif // MUTEXES_H