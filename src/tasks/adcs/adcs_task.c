/**
 * adcs_task.c
 *
 * RTOS task for ADCS functionality
 *
 * Created: September 20, 2025
 * Modified: November 9, 2025
 * Authors: Avinash Patel, Yi Lyo, Alexander Thaep
 */

#include "adcs_task.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

/**
 * \fn get_photomag_read_command
 *
 * \brief Creates a command to read magnetometer and photodiode data
 *
 * \param data pointer to data structure to fill
 *
 * \returns command_t command structure
 */
command_t get_photomag_read_command(mag_data_t *const mag_data, photodiode_data_t *const photodiode_data) {
    photomag_read_args_t args = {
        .mag_buffer = mag_data,
        .photodiode_buffer = photodiode_data
    };

    return (command_t) {
        .target = p_adcs_task,
        .operation = OPERATION_READ,
        .p_data = &args,
        .len = sizeof(photomag_read_args_t),
        .result = PROCESSING,
        .callback = NULL
    };
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn exec_command_photomag
 *
 * \brief Executes function corresponding to the command
 *
 * \param p_cmd a pointer to a command forwarded to magnetometer and photodiode
 */
void exec_command_photomag(command_t *const p_cmd) {
    if (p_cmd->target != p_adcs_task) {
        fatal("photo/mag: command target is not adcs! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
    }

    switch (p_cmd->operation) {
        case OPERATION_READ:
            photomag_read_args_t *args = (photomag_read_args_t *)p_cmd->p_data;
            status_t magnetometer_status = mag_read_data(args->mag_buffer);
            status_t photodiode_status = photodiode_read(args->photodiode_buffer);

            if (photodiode_status == SUCCESS && magnetometer_status == SUCCESS) p_cmd->result = SUCCESS;
            p_cmd->result = ERROR_READ_FAILED;

            break;
        default:
            fatal("photodiode: Invalid operation!\n");
            p_cmd->result = ERROR_SANITY_CHECK_FAILED;
            break;
    }
}