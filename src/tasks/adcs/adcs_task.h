#ifndef ADCS_H
#define ADCS_H

// Includes
#include <stddef.h>

#include "atmel_start.h"
#include "drivers/gyro/SCH1.h"
#include "drivers/magnetometer/magnetometer_driver.h"
#include "drivers/magnetorquer/magnetorquer_driver.h"
#include "drivers/photodiode/photodiode_driver.h"
#include "drivers/rtc/rtc_driver.h"
#include "float.h"
#include "globals.h"
#include "queue.h"

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

// TODO update this to properly reflect updated logic
struct adcs_data {
    photodiode_data_t *photodiode_buffer;
    size_t photodiode_data_len;
    mag_data_t *mag_buffer;
    size_t mag_buffer_len;
    rtc_data_t *rtc_buffer;
    size_t rtc_buffer_len;
};

typedef float_3d_t sun_vector_t;

// Function declarations
QueueHandle_t init_adcs(void);
void main_adcs(void *pvParameters);
command_t get_photomagrtc_read_command(void);
command_t get_adcs_process_command(adcs_data_t *const data);
void exec_command_adcs_process(command_t *const p_cmd);
sun_vector_t compute_sun_vector(photodiode_data_t *input);
bool tumbling(SCH1_result_t *gyro_data);         // TODO define
bool in_sun(photodiode_data_t *photodiode_data); // TODO define
void ukf_orient(/* TODO some inputs */ void);    // TODO define

#endif // ADCS_H
