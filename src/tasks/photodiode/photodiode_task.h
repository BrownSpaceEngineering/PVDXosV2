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
#define PHOTODIODE_COUNT 22   // Number of photodiodes (8 mux + 14 direct)

#define PHOTODIODE_S0_PIN (Photodiode_MUX_S0 & 0x1Fu)
#define PHOTODIODE_MUX_MASK (0xFu << PHOTODIODE_S0_PIN)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t photodiode_task_stack[PHOTODIODE_TASK_STACK_SIZE];
    uint8_t photodiode_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t photodiode_task_queue;
    StaticTask_t photodiode_task_tcb;
} photodiode_task_memory_t;

// Photodiode data structures
typedef struct {
    uint16_t raw_values[PHOTODIODE_COUNT];        // Raw ADC readings (up to 22)
    uint32_t timestamp;                               // Reading timestamp
    bool valid;                                       // Data validity flag
} photodiode_data_t;

typedef struct {
    photodiode_data_t *data_buffer;
} photodiode_read_args_t;

// Global memory and configuration
extern photodiode_task_memory_t photodiode_mem;

// Function declarations
QueueHandle_t init_photodiode(void);
void main_photodiode(void *pvParameters);
void exec_command_photodiode(command_t *const p_cmd);
status_t photodiode_read(photodiode_data_t *const data);
command_t get_photodiode_read_command(photodiode_data_t *const data);

#endif // PHOTODIODE_H
