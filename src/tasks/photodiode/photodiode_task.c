/**
 * photodiode_helpers.c
 *
 * Created: September 20, 2024
 * Authors:
 */

#include "photodiode_task.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */


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
        fatal("Failed to create command queue!\n");
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
        case OPERATION_PHOTODIODE_CALIBRATE:
            p_cmd->result = photodiode_calibrate();
            break;
        default:
            fatal("photodiode: Invalid operation!\n");
            p_cmd->result = ERROR_SANITY_CHECK_FAILED; // TODO: appropriate status?
            break;
    }
}
/* ---------- PHOTODIODE-SPECIFIC FUNCTIONS ---------- */

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
    
    debug("photodiode: Reading photodiode values\n");
    
    // TODO: Implement ADC reading for all photodiodes
    // Read raw ADC values
    uint16_t raw_values[PHOTODIODE_COUNT];
    ret_err_status(read_photodiode_adc(raw_values, PHOTODIODE_COUNT), "photodiode: ADC read failed");
    
    // Copy raw values to data structure
    for (int i = 0; i < PHOTODIODE_COUNT; i++) {
        data->raw_values[i] = raw_values[i];
    }
    
    // Calibrate readings
    ret_err_status(calibrate_photodiode_readings(raw_values, data->calibrated_values, PHOTODIODE_COUNT), "photodiode: Calibration failed");
    
    // Calculate sun vector
    ret_err_status(calculate_sun_vector(data->calibrated_values, data->sun_vector), "photodiode: Sun vector calculation failed");    // TODO: Implement calibration calculations
    // TODO: Calculate sun vector from photodiode readings
    
    data->timestamp = xTaskGetTickCount();
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

