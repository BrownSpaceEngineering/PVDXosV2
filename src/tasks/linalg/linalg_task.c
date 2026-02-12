/**
 * linalg_helpers.c
 *
 * Created: February 08, 2026
 * Authors:
 */

#include "linalg_task.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */


/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn init_linalg
 *
 * \brief Initialises linalg command queue, before `init_task_pointer()`.
 *
 * \returns QueueHandle_t, a handle to the created queue
 *
 * \see `init_task_pointer()` for usage of functions of the type `init_<TASK>()`
 */
QueueHandle_t init_linalg(void) {
    QueueHandle_t linalg_command_queue_handle = xQueueCreateStatic(
        COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, linalg_mem.linalg_command_queue_buffer,
        &linalg_mem.linalg_task_queue);

    if (linalg_command_queue_handle == NULL) {
        fatal("Failed to create command queue!\n");
    }

    return linalg_command_queue_handle;
}

/**
 * \fn exec_command_linalg
 * 
 * \brief Executes function corresponding to the command
 * 
 * \param p_cmd a pointer to a command forwarded to linalg
 */
void exec_command_linalg(command_t *const p_cmd) {
    if (p_cmd->target != p_linalg_task) {
        fatal("linalg: command target is not linalg! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
    }

    switch (p_cmd->operation) {
        default:
            fatal("linalg: Invalid operation!\n");
            p_cmd->result = ERROR_SANITY_CHECK_FAILED; // TODO: appropriate status?
            break;
    }
}