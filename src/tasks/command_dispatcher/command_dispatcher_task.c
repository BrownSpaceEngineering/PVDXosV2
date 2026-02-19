/**
 * command_dispatcher_task.c
 *
 * Helper functions for the Command Dispatcher task. This task is responsible for receiving
 * commands from other tasks and forwarding them to the appropriate task for execution. All major
 * commands MUST be sent through the Command Dispatcher task to enable consistent logging and adhere
 * to the PVDXos hub-and-spoke architecture.
 *
 * Created: October 13, 2024
 * Authors: Tanish Makadia, Yi Liu, Siddharta Laloux, Ignacio Blancas Rodriguez
 */

#include "command_dispatcher_task.h"

#include "task_list.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

// NOTE: No dispatchable functions for the command dispatcher task. Its sole purpose is to
// forward commands to other tasks. Essentially, it's a glorified queue.

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn init_commamd_dispatecher
 *
 * \brief Initializes command dispatcher queue, before `init_task_pointer()`.
 *
 * \returns QueueHandle_t, a handle to the created queue
 *
 * \see `init_task_pointer()` for usage of functions of the type `init_<TASK>()`
 */
QueueHandle_t init_command_dispatcher(void) {
    QueueHandle_t command_dispatcher_command_queue_handle = xQueueCreateStatic(
        COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, command_dispatcher_mem.command_dispatcher_command_queue_buffer,
        &command_dispatcher_mem.command_dispatcher_task_queue);

    if (command_dispatcher_command_queue_handle == NULL) {
        fatal("Failed to create command queue!\n");
    }

    return command_dispatcher_command_queue_handle;
}

/**
 * \fn enqueue_command
 *
 * \brief Enqueue a command to be forwarded by the command dispatcher
 *
 * \param p_cmd a pointer to the command struct to be enqueued
 */
void enqueue_command(command_t *const p_cmd) {
    if (xQueueSendToBack(p_command_dispatcher_task->command_queue, p_cmd, 0) != pdTRUE) {
        fatal("%s task failed to enqueue command onto Command Dispatcher queue!\n", get_current_task()->name);
    }
}

/**
 * \fn dispatch_command
 *
 * \brief Forward a dequeued command to the appropriate task for execution
 *
 * \param p_cmd a pointer to the command struct to be dispatched
 *
 * \return status_t, whether the forwarding was successful or not
 *
 * \warning produces `ERROR_BAD_TARGET` if target null
 * \warning produces `ERROR_TASK_DISABLED` if target disabled
 * \warning `fatal` error if command cannot be forwarded to queue
 */
status_t dispatch_command(command_t *const p_cmd) {
    // Check if the task is non-NULL
    // TODO: make this check exhaustive 
    if (p_cmd->target == NULL) {
        return ERROR_BAD_TARGET;
    }

    // Check if the task to dispatch to was disabled
    if (!p_cmd->target->enabled) {
        return ERROR_TASK_DISABLED;
    }

    if (xQueueSendToBack(p_cmd->target->command_queue, p_cmd, 0) != pdTRUE) {
        fatal("command-dispatcher: Failed to forward command to %s task\n", p_cmd->target->name);
    }

    debug("command-dispatcher: Forwarded a command to %s task\n", p_cmd->target->name);

    return SUCCESS;
}
