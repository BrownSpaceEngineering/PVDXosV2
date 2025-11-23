/**
 * adcs_task.c
 * 
 * RTOS task for Attitude Determination and Control System (ADCS)
 *
 * Created: Nov 23, 2025
 * Authors: Yi Lyo
 */

#include "adcs_task.h"

extern adcs_task_memory_t adcs_mem;

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

// TODO: Add ADCS-specific dispatchable functions here

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn exec_command_adcs
 * 
 * \brief Executes a command received by the ADCS task
 * 
 * \param p_cmd Pointer to the received command
 */
void exec_command_adcs(command_t *const p_cmd) {
    if (p_cmd->target != p_adcs_task) {
        fatal("adcs: command target is not ADCS! target: %s operation: %d\n", p_cmd->target->name, p_cmd->operation);
    }

    switch (p_cmd->operation) {
        // TODO: Add ADCS-specific operations here
        default:
            fatal("adcs: Invalid operation! target: %s operation: %d\n", p_cmd->target->name, p_cmd->operation);
            break;
    }
}

/**
 * \fn init_adcs
 * 
 * \brief Initializes the ADCS task, including hardware setup and command queue creation
 * 
 * \return Handle to the ADCS task's command queue
 */
QueueHandle_t init_adcs(void) {
    // TODO: Add hardware initialization here
    // fatal_on_error(init_adcs_hardware(), "adcs: Hardware initialization failed!");

    // Initialize the ADCS command queue
    QueueHandle_t adcs_command_queue_handle =
        xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, adcs_mem.adcs_command_queue_buffer,
                           &adcs_mem.adcs_task_queue);
    if (adcs_command_queue_handle == NULL) {
        fatal("Failed to create ADCS queue!\n");
    }

    return adcs_command_queue_handle;
}
