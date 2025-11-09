#ifndef ADCS_H
#define ADCS_H

// Includes
#include "globals.h"
#include "logging.h"
#include "queue.h"
#include "task_list.h"
#include "atmel_start.h"
#include "watchdog_task.h"
#include "photodiode_driver.h"

// TODO: cool ascii art
#define ADCS_ASCII_ART                                                                                                                    \
    "  ______     \n"                                                                                                           \
    " /  __  \\   \n"                                                                                             \
    "|  /  \\  |  \n"                                                                                             \
    "|  \\__/| |  \n"                                                                                             \
    "|_|     |_|  \n"

// Constants
#define ADCS_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Photodiode system constants
#define PHOTODIODE_COUNT 22   // Number of photodiodes (8 mux + 14 direct)

#define PHOTODIODE_S0_PIN (Photodiode_MUX_S0 & 0x1Fu)
#define PHOTODIODE_MUX_MASK (0xFu << PHOTODIODE_S0_PIN)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t adcs_task_stack[ADCS_TASK_STACK_SIZE];
    uint8_t adcs_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t adcs_task_queue;
    StaticTask_t adcs_task_tcb;
} adcs_task_memory_t;

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
extern adcs_task_memory_t adcs_mem;

// Function declarations
QueueHandle_t init_adcs(void);
void main_adcs(void *pvParameters);
void exec_command_photodiode(command_t *const p_cmd);
status_t photodiode_read(photodiode_data_t *const data);
command_t get_photodiode_read_command(photodiode_data_t *const data);

#endif // ADCS_H
