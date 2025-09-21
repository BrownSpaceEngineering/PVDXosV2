#ifndef PHOTODIODE_H
#define PHOTODIODE_H

// Includes
#include "globals.h"
#include "logging.h"
#include "queue.h"
#include "task_list.h"

// Constants
#define PHOTODIODE_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t photodiode_task_stack[PHOTODIODE_TASK_STACK_SIZE];
    uint8_t photodiode_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t photodiode_task_queue;
    StaticTask_t photodiode_task_tcb;
} photodiode_task_memory_t;

extern photodiode_task_memory_t photodiode_mem;

QueueHandle_t init_photodiode(void);
void main_photodiode(void *pvParameters);

#endif // PHOTODIODE_H
// Photodiode-specific includes
#include "atmel_start.h"
#include "watchdog_task.h"
#include "photodiode_driver.h"
// Photodiode constants
#define PHOTODIODE_COUNT 6  // Number of photodiodes for 3D sun sensing
#define PHOTODIODE_ADC_CHANNELS 6  // ADC channels for photodiode readings

// Photodiode data structures
typedef struct {
    uint16_t raw_values[PHOTODIODE_COUNT];        // Raw ADC readings
    float calibrated_values[PHOTODIODE_COUNT];    // Calibrated light intensities
    float sun_vector[3];                          // Calculated sun direction vector
    uint32_t timestamp;                           // Reading timestamp
    bool valid;                                   // Data validity flag
} photodiode_data_t;

typedef struct {
    photodiode_data_t *data_buffer;
    bool request_calibration;
} photodiode_read_args_t;

// Function declarations
void exec_command_photodiode(command_t *const p_cmd);
status_t photodiode_read(photodiode_data_t *const data);
status_t photodiode_calibrate(void);
command_t get_photodiode_read_command(photodiode_data_t *const data);

