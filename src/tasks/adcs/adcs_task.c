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
        .data_buffer = data
    };

    return (command_t) {
        .target = p_adcs_task,
        .operation = OPERATION_PHOTODIODE_READ,
        .p_data = &args,
        .len = sizeof(photodiode_read_args_t),
        .result = PROCESSING,
        .callback = NULL
    };
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn exec_command_photodiode
 *
 * \brief Executes function corresponding to the command
 *
 * \param p_cmd a pointer to a command forwarded to photodiode
 */
void exec_command_photodiode(command_t *const p_cmd) {
    if (p_cmd->target != p_adcs_task) {
        fatal("photodiode: command target is not adcs! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
    }

    switch (p_cmd->operation) {
        case OPERATION_PHOTODIODE_READ:
            photodiode_read_args_t *args = (photodiode_read_args_t *)p_cmd->p_data;
            p_cmd->result = photodiode_read(args->data_buffer);
            break;
        default:
            fatal("photodiode: Invalid operation!\n");
            p_cmd->result = ERROR_SANITY_CHECK_FAILED;
            break;
    }
}