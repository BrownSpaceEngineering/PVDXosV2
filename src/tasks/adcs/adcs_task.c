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
 * \fn get_adcs_process_command
 *
 * \brief Creates a command to do adcs stuff
 *
 * \param data pointer to data structure to fill
 *
 * \returns command_t command structure
 */
command_t get_adcs_process_command(photomagrtc_read_args_t *const args) {
    return (command_t) {
        .target = p_adcs_task,
        .operation = OPERATION_PROCESS,
        .p_data = &args,
        .len = sizeof(photomagrtc_read_args_t),
        .result = PROCESSING,
        .callback = NULL
    };
}

/**
 * \fn get_photomagrtc_read_command
 *
 * \brief Creates a command to read magnetometer, photodiode, rtc data
 *
 * \param data pointer to data structure to fill
 *
 * \returns command_t command structure
 */
command_t get_photomagrtc_read_command(
        mag_data_t *const mag_data, 
        photodiode_data_t *const photodiode_data,
        rtc_data_t *const rtc_data) 
    {
    photomagrtc_read_args_t args = {
        .mag_buffer = mag_data,
        .photodiode_buffer = photodiode_data,
        .rtc_buffer = rtc_data
    };

    return (command_t) {
        .target = p_adcs_task,
        .operation = OPERATION_READ,
        .p_data = &args,
        .len = sizeof(photomagrtc_read_args_t),
        .result = PROCESSING,
        .callback = NULL
    };
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn exec_command_photomagrtc
 *
 * \brief Executes function corresponding to the command
 *
 * \param p_cmd a pointer to a command forwarded to magnetometer, photodiode, and rtc
 */
void exec_command_photomagrtc(command_t *const p_cmd) {
    if (p_cmd->target != p_adcs_task) {
        fatal("photo/mag: command target is not adcs! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
    }

    photomagrtc_read_args_t *args = (photomagrtc_read_args_t *)p_cmd->p_data;
    status_t magnetometer_status = mag_read_data(args->mag_buffer);
    status_t photodiode_status = photodiode_read(args->photodiode_buffer);
    status_t rtc_status = get_rtc_values(args->rtc_buffer);

    if (photodiode_status == SUCCESS 
        && magnetometer_status == SUCCESS
        && rtc_status == SUCCESS) p_cmd->result = SUCCESS;
    p_cmd->result = ERROR_READ_FAILED;
}

/**
 * \fn exec_command_adcs_process
 *
 * \brief Executes function corresponding to the command
 *
 * \param p_cmd a pointer to a command containing information for processing
 */
void exec_command_adcs_process(command_t *const p_cmd) {
    if (p_cmd->target != p_adcs_task) {
        fatal("adcs processing: command target is not adcs! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
    }

    rtc_data_t temp;

    photomagrtc_read_args_t *args = (photomagrtc_read_args_t *)p_cmd->p_data;
    status_t rtc_status = get_rtc_values(&temp);

    if (args == NULL) info("adcs: stuff happens here\n");

    // Do stuff with readings here

    if (rtc_status == SUCCESS) p_cmd->result = SUCCESS;
    p_cmd->result = ERROR_PROCESSING_FAILED;
}