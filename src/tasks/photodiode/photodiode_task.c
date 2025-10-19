/**
 * photodiode_task.c
 *
 * RTOS task for photodiode sensors used in ADCS sun sensing.
 * Supports 13-21 photodiodes with configurable sampling rates (0.1-100 Hz).
 *
 * Created: September 20, 2024
 * Authors: Avinash Patel
 */

#include "photodiode_task.h"

// Global configuration
photodiode_config_t photodiode_config = {
    .photodiode_count = PHOTODIODE_DEFAULT_COUNT,
    .delay_ms = PHOTODIODE_DEFAULT_DELAY_MS,
    .mux_select_pins = {Photodiode_MUX_S0, Photodiode_MUX_S1, Photodiode_MUX_S2}, // GPIO pins for MUX select lines (S0-S2)
    .mux_enable_pin = Photodiode_MUX_EN, // GPIO pin for multiplexer enable/disable
    .use_multiplexer = true
};

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

/**
 * \fn photodiode_read
 *
 * \brief Reads photodiode values and calculates sun vector
 *
 * \param data pointer to photodiode_data_t structure to fill
 *
 * \returns status_t SUCCESS if reading was successful
 */
status_t photodiode_read(photodiode_data_t *const data) {
    if (!data) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    debug("photodiode: Reading %d photodiode values\n", photodiode_config.photodiode_count);
    
    // Read raw ADC values
    uint16_t raw_values[PHOTODIODE_MAX_COUNT];
    status_t result = read_photodiode_adc(raw_values, photodiode_config.photodiode_count);
    
    if (result != SUCCESS) {
        warning("photodiode: ADC read failed\n");
        return result;
    }
    
    // Copy raw values to data structure
    for (int i = 0; i < photodiode_config.photodiode_count; i++) {
        data->raw_values[i] = raw_values[i];
    }
    
    // Calibrate readings
    ret_err_status(calibrate_photodiode_readings(raw_values, data->calibrated_values, 
                                                photodiode_config.photodiode_count), 
                   "photodiode: Calibration failed");
    
    // Calculate sun vector
    ret_err_status(calculate_sun_vector(data->calibrated_values, data->sun_vector), 
                   "photodiode: Sun vector calculation failed");
    
    data->timestamp = xTaskGetTickCount();
    data->active_count = photodiode_config.photodiode_count;
    data->valid = true;
    
    return SUCCESS;
}

/**
 * \fn photodiode_calibrate
 *
 * \brief Calibrates photodiode readings
 *
 * \returns status_t SUCCESS if calibration was successful
 */
status_t photodiode_calibrate(void) {
    debug("photodiode: Calibrating photodiode readings\n");
    
    // TODO: Implement photodiode calibration
    // TODO: Store calibration values
    
    return SUCCESS;
}

/**
 * \fn photodiode_set_config
 *
 * \brief Sets photodiode configuration (count, sampling rate, etc.)
 *
 * \param config pointer to photodiode configuration
 *
 * \returns status_t SUCCESS if configuration was successful
 */
status_t photodiode_set_config(const photodiode_config_t *const config) {
    if (!config) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    // Validate configuration
    if (config->photodiode_count < PHOTODIODE_MIN_COUNT || 
        config->photodiode_count > PHOTODIODE_MAX_COUNT) {
        warning("photodiode: Invalid photodiode count: %d (must be %d-%d)\n", 
                config->photodiode_count, PHOTODIODE_MIN_COUNT, PHOTODIODE_MAX_COUNT);
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    if (config->delay_ms < PHOTODIODE_MIN_DELAY_MS || 
        config->delay_ms > PHOTODIODE_MAX_DELAY_MS) {
        warning("photodiode: Invalid delay: %d ms (must be %d-%d ms)\n", 
                config->delay_ms, PHOTODIODE_MIN_DELAY_MS, PHOTODIODE_MAX_DELAY_MS);
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    // Update configuration
    photodiode_config = *config;
    
    info("photodiode: Configuration updated - Count: %d, Delay: %d ms\n",
         config->photodiode_count, config->delay_ms);
    
    return SUCCESS;
}

/**
 * \fn get_photodiode_read_command
 *
 * \brief Creates a command to read photodiode data
 *
 * \param data pointer to data structure to fill
 *
 * \returns command_t command structure
 */
command_t get_photodiode_read_command(photodiode_data_t *const data) {
    photodiode_read_args_t args = {
        .data_buffer = data,
        .request_calibration = false
    };
    
    return (command_t) {
        .target = p_photodiode_task,
        .operation = OPERATION_PHOTODIODE_READ,
        .p_data = &args,
        .len = sizeof(photodiode_read_args_t),
        .result = PROCESSING,
        .callback = NULL
    };
}

/**
 * \fn get_photodiode_config_command
 *
 * \brief Creates a command to configure photodiode settings
 *
 * \param config pointer to configuration structure
 *
 * \returns command_t command structure
 */
command_t get_photodiode_config_command(const photodiode_config_t *const config) {
    photodiode_config_args_t args = {
        .config = (photodiode_config_t *)config
    };
    
    return (command_t) {
        .target = p_photodiode_task,
        .operation = OPERATION_PHOTODIODE_CONFIG, // Reuse for config
        .p_data = &args,
        .len = sizeof(photodiode_config_args_t),
        .result = PROCESSING,
        .callback = NULL
    };
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn init_photodiode
 *
 * \brief Initialises photodiode command queue, before `init_task_pointer()`.
 *
 * \returns QueueHandle_t, a handle to the created queue
 *
 * \see `init_task_pointer()` for usage of functions of the type `init_<TASK>()`
 */
QueueHandle_t init_photodiode(void) {
    QueueHandle_t photodiode_command_queue_handle = xQueueCreateStatic(
        COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, photodiode_mem.photodiode_command_queue_buffer,
        &photodiode_mem.photodiode_task_queue);

    if (photodiode_command_queue_handle == NULL) {
        fatal("Failed to create photodiode command queue!\n");
    }

    // Initialize photodiode hardware
    status_t result = init_photodiode_hardware();
    if (result != SUCCESS) {
        warning("photodiode: Hardware initialization failed\n");
    }

    return photodiode_command_queue_handle;
}

/**
 * \fn exec_command_photodiode
 * 
 * \brief Executes function corresponding to the command
 * 
 * \param p_cmd a pointer to a command forwarded to photodiode
 */
void exec_command_photodiode(command_t *const p_cmd) {
    if (p_cmd->target != p_photodiode_task) {
        fatal("photodiode: command target is not photodiode! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
    }

    switch (p_cmd->operation) {
        case OPERATION_PHOTODIODE_READ:
            {
                photodiode_read_args_t *args = (photodiode_read_args_t *)p_cmd->p_data;
                p_cmd->result = photodiode_read(args->data_buffer);
            }
            break;
        case OPERATION_PHOTODIODE_CONFIG:
            {
                photodiode_config_args_t *args = (photodiode_config_args_t *)p_cmd->p_data;
                p_cmd->result = photodiode_set_config(args->config);
            }
            break;
        case OPERATION_PHOTODIODE_CALIBRATE:
            p_cmd->result = photodiode_calibrate();
            break;
        default:
            fatal("photodiode: Invalid operation!\n");
            p_cmd->result = ERROR_SANITY_CHECK_FAILED;
            break;
    }
}
