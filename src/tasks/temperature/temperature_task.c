#include "temperature_task.h"
#include "task_list.h"

status_t temperature_read(temperature_data_t *const data) {
    if (!data) {
        return ERROR_SANITY_CHECK_FAILED;
    }

    temp_sensor_sample_t sample = {0};
    status_t result = temp_sensor_sample(&sample);
    if (result != SUCCESS) {
        warning("temperature: sensor sample failed (%d)\n", result);
        return result;
    }

    data->sample = sample;
    data->timestamp = xTaskGetTickCount();
    data->valid = true;

    return SUCCESS;
}

command_t get_temperature_read_command(temperature_data_t *const data) {
    temperature_read_args_t args = {
        .data_buffer = data
    };

    return (command_t){
        .target = p_temperature_task,
        .operation = OPERATION_TEMPERATURE_READ,
        .p_data = &args,
        .len = sizeof(temperature_read_args_t),
        .result = PROCESSING,
        .callback = NULL
    };
}

QueueHandle_t init_temperature(void) {
    QueueHandle_t queue_handle = xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE,
        temperature_mem.temperature_command_queue_buffer, &temperature_mem.temperature_task_queue);

    if (queue_handle == NULL) {
        fatal("temperature: failed to create command queue\n");
    }

    status_t status = temp_sensor_init();
    if (status != SUCCESS) {
        warning("temperature: hardware init failed (%d)\n", status);
    }

    return queue_handle;
}

void exec_command_temperature(command_t *const p_cmd) {
    if (!p_cmd) {
        return;
    }

    if (p_cmd->target != p_temperature_task) {
        fatal("temperature: invalid command target (target %p)\n", (void *)p_cmd->target);
        p_cmd->result = ERROR_SANITY_CHECK_FAILED;
        return;
    }

    switch (p_cmd->operation) {
        case OPERATION_TEMPERATURE_READ: {
            temperature_read_args_t *args = (temperature_read_args_t *)p_cmd->p_data;
            p_cmd->result = temperature_read(args ? args->data_buffer : NULL);
            break;
        }
        default:
            fatal("temperature: unsupported operation %d\n", p_cmd->operation);
            p_cmd->result = ERROR_SANITY_CHECK_FAILED;
            break;
    }
}

