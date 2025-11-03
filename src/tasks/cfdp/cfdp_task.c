/**
 * cfdp_helpers.c
 *
 * Created: October 30, 2025
 * Authors: Siddharta Laloux
 */

#include "cfdp_task.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */


/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn init_cfdp
 *
 * \brief Initialises cfdp command queue, before `init_task_pointer()`.
 *
 * \returns QueueHandle_t, a handle to the created queue
 *
 * \see `init_task_pointer()` for usage of functions of the type `init_<TASK>()`
 */
QueueHandle_t init_cfdp(void) {
    QueueHandle_t cfdp_command_queue_handle = xQueueCreateStatic(
        COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, cfdp_mem.cfdp_command_queue_buffer,
        &cfdp_mem.cfdp_task_queue);

    if (cfdp_command_queue_handle == NULL) {
        fatal("Failed to create command queue!\n");
    }

    return cfdp_command_queue_handle;
}

/**
 * \fn exec_command_cfdp
 * 
 * \brief Executes function corresponding to the command
 * 
 * \param p_cmd a pointer to a command forwarded to cfdp
 */
void exec_command_cfdp(command_t *const p_cmd) {
    if (p_cmd->target != p_cfdp_task) {
        fatal("cfdp: command target is not cfdp! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
    }

    switch (p_cmd->operation) {
        default:
            fatal("cfdp: Invalid operation!\n");
            p_cmd->result = ERROR_SANITY_CHECK_FAILED; // TODO: appropriate status?
            break;
    }
}