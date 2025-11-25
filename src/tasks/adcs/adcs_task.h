#ifndef ADCS_H
#define ADCS_H

// Includes
#include "atmel_start.h"
#include "globals.h"
#include "logging.h"
#include "magnetometer_driver.h"
#include "photodiode_driver.h"
#include "rtc_driver.h"
#include "queue.h"
#include "task_list.h"
#include "watchdog_task.h"

// TODO: cool ascii art
#define ADCS_ASCII_ART                                                                                                                     \
    "    _    ____   ____ ____     \n"                                                                                                     \
    "   / \\  |  _ \\ / ___/ ___|  \n"                                                                                                     \
    "  / _ \\ | | | | |   \\___ \\ \n"                                                                                                     \
    " / ___ \\| |_| | |___ ___) |  \n"                                                                                                     \
    "/_/   \\_\\____/ \\____|____/ \n"

// Constants
#define ADCS_TASK_STACK_SIZE 1024 // Size of the stack in words (multiply by 4 to get bytes)

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t adcs_task_stack[ADCS_TASK_STACK_SIZE];
    uint8_t adcs_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t adcs_task_queue;
    StaticTask_t adcs_task_tcb;
} adcs_task_memory_t;

// Global memory and configuration
extern adcs_task_memory_t adcs_mem;

typedef struct {
    photodiode_data_t *photodiode_buffer;
    mag_data_t *mag_buffer;
} photomag_read_args_t;

// Function declarations
QueueHandle_t init_adcs(void);
void main_adcs(void *pvParameters);
command_t get_photomag_read_command(mag_data_t *const mag_data, photodiode_data_t *const photodiode_data);
void exec_command_photomag(command_t *const p_cmd);

#endif // ADCS_H
