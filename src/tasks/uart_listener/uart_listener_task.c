/**
 * uart_listener_helpers.c
 *
 * Created: November 13, 2025
 * Authors:
 */

#include "uart_listener_task.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */


/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn init_uart_listener
 *
 * \brief Initialises uart_listener command queue, before `init_task_pointer()`.
 *
 * \returns QueueHandle_t, a handle to the created queue
 *
 * \see `init_task_pointer()` for usage of functions of the type `init_<TASK>()`
 */
QueueHandle_t init_uart_listener(void) {
    QueueHandle_t uart_listener_command_queue_handle = xQueueCreateStatic(
        COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, uart_listener_mem.uart_listener_command_queue_buffer,
        &uart_listener_mem.uart_listener_task_queue);

    if (uart_listener_command_queue_handle == NULL) {
        fatal("Failed to create command queue!\n");
    }

    return uart_listener_command_queue_handle;
}

/**
 * \fn exec_command_uart_listener
 * 
 * \brief Executes function corresponding to the command
 * 
 * \param p_cmd a pointer to a command forwarded to uart_listener
 */
void exec_command_task_manager(command_t *const p_cmd) {
    if (p_cmd->target != p_uart_listener_task) {
        fatal("uart_listener: command target is not uart_listener! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
    }

    switch (p_cmd->operation) {
        default:
            fatal("uart_listener: Invalid operation!\n");
            p_cmd->result = ERROR_SANITY_CHECK_FAILED; // TODO: appropriate status?
            break;
    }
}