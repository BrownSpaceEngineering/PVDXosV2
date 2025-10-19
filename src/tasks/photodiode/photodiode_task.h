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
#define PHOTODIODE_MAX_COUNT 22        // Maximum number of photodiodes (8 multiplexed + 14 direct)
#define PHOTODIODE_MIN_COUNT 1         // Minimum number of photodiodes
#define PHOTODIODE_DEFAULT_COUNT 22   // Default number of photodiodes (8 mux + 14 direct)
#define PHOTODIODE_ADC_CHANNELS 15     // 1 multiplexed + 14 direct ADC channels
#define PHOTODIODE_MUX_SELECT_BITS 3   // 3 bits needed for 8 channels (2^3 = 8)
#define PHOTODIODE_MUX_CHANNELS 8      // 8 photodiodes on multiplexer (channels 0-7)
#define PHOTODIODE_DIRECT_CHANNELS 14  // 14 photodiodes on direct ADC (channels 8-21)
#define PHOTODIODE_MUX_SETTLE_TIME_MS 1 // Multiplexer settling time in milliseconds

// Sampling rate constants (delay in ms)
#define PHOTODIODE_MIN_DELAY_MS 10      // Minimum: 10ms (100 Hz)
#define PHOTODIODE_MAX_DELAY_MS 10000   // Maximum: 10000ms (0.1 Hz)  
#define PHOTODIODE_DEFAULT_DELAY_MS 1000 // Default: 1000ms (1 Hz)

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
    uint8_t photodiode_count;      // Number of active photodiodes (8-22)
    uint32_t delay_ms;             // Sampling delay in milliseconds
    uint8_t mux_select_pins[PHOTODIODE_MUX_SELECT_BITS]; // GPIO pins for multiplexer select lines
    uint8_t mux_enable_pin;        // GPIO pin for multiplexer enable/disable
    bool use_multiplexer;          // Enable multiplexer mode
} photodiode_config_t;

// Photodiode data structures
typedef struct {
    uint16_t raw_values[PHOTODIODE_MAX_COUNT];        // Raw ADC readings (up to 22)
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
