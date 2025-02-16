/**
 * command_dispatcher_helpers.c
 * 
 * Helper functions for the Command Dispatcher task. This task is responsible for receiving
 * commands from other tasks and forwarding them to the appropriate task for execution. All major
 * commands MUST be sent through the Command Dispatcher task to enable consistent logging and adhere
 * to the PVDXos hub-and-spoke architecture.
 * 
 * Created: October 13, 2024
 * Authors: Tanish Makadia, Yi Liu
 */

#include "command_dispatcher_task.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

// NOTE: No dispatchable functions for the command dispatcher task. Its sole purpose is to
// forward commands to other tasks. Essentially, it's a glorified queue.

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

// Initialize the command queue, which stores pointers to command structs
void init_command_dispatcher(void) {
    command_dispatcher_command_queue_handle = xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, command_dispatcher_command_queue_buffer,
                                                    &command_dispatcher_mem.command_dispatcher_task_queue);

    if (command_dispatcher_command_queue_handle == NULL) {
        fatal("Failed to create command queue!\n");
    }
}

// Enqueue a command to be executed by the command dispatcher
void enqueue_command(command_t *const p_cmd) {    
    if (xQueueSendToBack(command_dispatcher_command_queue_handle, p_cmd, 0) != pdTRUE) {
        pvdx_task_t* calling_task = get_task(xTaskGetCurrentTaskHandle());
        fatal("%s task failed to enqueue command onto Command Dispatcher queue!\n", calling_task->name);
    }
}

// Forward a dequeued command to the appropriate task for execution
void dispatch_command(command_t *const p_cmd) {
    switch (p_cmd->target) {
        case TASK_MANAGER:
            if (xQueueSendToBack(task_manager_command_queue_handle, p_cmd, 0) != pdTRUE) {
                fatal("command-dispatcher: Failed to forward command to task manager task!\n");
            }

            debug("command-dispatcher: Forwarded a command to task manager task\n");
            break;
        case TASK_WATCHDOG:
            if (xQueueSendToBack(watchdog_command_queue_handle, p_cmd, 0) != pdTRUE) {
                fatal("command-dispatcher: Failed to forward command to watchdog task!\n");
            }
            
            debug("command_dispatcher: Forwarded a command to watchdog task\n");
            break;
        case TASK_SHELL:
            break;
        case TASK_HEARTBEAT:
            fatal("command-dispatcher: Heartbeat task does not accept commands!\n");
            break;
        case TASK_MAGNETOMETER:
            fatal("command-dispatcher: Failed; Magnetometer queue does not exist\n");
            break;
        case TASK_CAMERA:
            fatal("command-dispatcher: Failed; Camera queue does not exist\n");
            break;
        case TASK_9AXIS:
            fatal("command-dispatcher: Failed; 9Axis queue does not exist\n");
            break;
        case TASK_DISPLAY:
            if (xQueueSendToBack(display_command_queue_handle, p_cmd, 0) != pdTRUE) {
                fatal("command-dispatcher: Failed to forward command to display task!\n");
            }

            debug("command-dispatcher: Forwarded a command to display task\n");
            break;
        default:
            fatal("command-dispatcher: Invalid target task!\n");
            break;
    }
}
