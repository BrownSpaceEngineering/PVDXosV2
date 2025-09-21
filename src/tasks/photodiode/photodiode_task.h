#ifndef PHOTODIODE_H
#define PHOTODIODE_H

// Includes
#include "globals.h"
#include "logging.h"
#include "queue.h"
#include "task_list.h"
#include "atmel_start.h"
#include "watchdog_task.h"
#include "photodiode_driver.h"

// Constants
#define PHOTODIODE_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Photodiode system constants
#define PHOTODIODE_MAX_COUNT 21        // Maximum number of photodiodes (13-21 expected)
#define PHOTODIODE_MIN_COUNT 13        // Minimum number of photodiodes
#define PHOTODIODE_DEFAULT_COUNT 16    // Default number of photodiodes (matches ADC channels)
#define PHOTODIODE_ADC_CHANNELS 16     // Available ADC channels (can use multiplexing for more)

// Sampling rate constants (Hz)
#define PHOTODIODE_MIN_SAMPLE_RATE 0.1f    // Minimum: 0.1 Hz (1 reading every 10 seconds)
#define PHOTODIODE_MAX_SAMPLE_RATE 100.0f  // Maximum: 100 Hz
#define PHOTODIODE_DEFAULT_SAMPLE_RATE 1.0f // Default: 1 Hz (algorithm requirement >1 Hz)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t photodiode_task_stack[PHOTODIODE_TASK_STACK_SIZE];
    uint8_t photodiode_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t photodiode_task_queue;
    StaticTask_t photodiode_task_tcb;
} photodiode_task_memory_t;

// Photodiode configuration structure
typedef struct {
    uint8_t photodiode_count;      // Number of active photodiodes (13-21)
    float sample_rate_hz;          // Sampling rate in Hz (0.1-100)
    bool use_multiplexing;         // Whether to use ADC multiplexing
    uint8_t adc_channels[PHOTODIODE_MAX_COUNT]; // ADC channel mapping for each photodiode
} photodiode_config_t;

// Photodiode data structures
typedef struct {
    uint16_t raw_values[PHOTODIODE_MAX_COUNT];        // Raw ADC readings (up to 21)
    float calibrated_values[PHOTODIODE_MAX_COUNT];    // Calibrated light intensities
    float sun_vector[3];                              // Calculated sun direction vector
    uint32_t timestamp;                               // Reading timestamp
    uint8_t active_count;                             // Number of active photodiodes
    bool valid;                                       // Data validity flag
} photodiode_data_t;

typedef struct {
    photodiode_data_t *data_buffer;
    bool request_calibration;
} photodiode_read_args_t;

typedef struct {
    photodiode_config_t *config;
} photodiode_config_args_t;

// Global memory and configuration
extern photodiode_task_memory_t photodiode_mem;
extern photodiode_config_t photodiode_config;

// Function declarations
QueueHandle_t init_photodiode(void);
void main_photodiode(void *pvParameters);
void exec_command_photodiode(command_t *const p_cmd);
status_t photodiode_read(photodiode_data_t *const data);
status_t photodiode_calibrate(void);
status_t photodiode_set_config(const photodiode_config_t *const config);
command_t get_photodiode_read_command(photodiode_data_t *const data);
command_t get_photodiode_config_command(const photodiode_config_t *const config);

#endif // PHOTODIODE_H
