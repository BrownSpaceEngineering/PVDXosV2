#ifndef TEMPERATURE_TASK_H
#define TEMPERATURE_TASK_H

#include "globals.h"
#include "logging.h"
#include "queue.h"
#include "temperature_driver.h"
#include "watchdog_task.h"

#define TEMPERATURE_TASK_STACK_SIZE 512U

typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t temperature_task_stack[TEMPERATURE_TASK_STACK_SIZE];
    uint8_t temperature_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t temperature_task_queue;
    StaticTask_t temperature_task_tcb;
} temperature_task_memory_t;

typedef struct {
    temp_sensor_sample_t sample;
    uint32_t timestamp;
    bool valid;
} temperature_data_t;

typedef struct {
    temperature_data_t *data_buffer;
} temperature_read_args_t;

extern temperature_task_memory_t temperature_mem;

QueueHandle_t init_temperature(void);
void main_temperature(void *pvParameters);
void exec_command_temperature(command_t *const p_cmd);
status_t temperature_read(temperature_data_t *const data);
command_t get_temperature_read_command(temperature_data_t *const data);

#endif // TEMPERATURE_TASK_H

