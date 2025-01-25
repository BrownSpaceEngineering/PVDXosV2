#include "command_dispatcher_task.h"

// Initialize the command queue, which stores pointers to command structs
void init_command_dispatcher(void) {
    command_dispatcher_command_queue_handle = xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, command_dispatcher_command_queue_buffer,
                                                    &command_dispatcher_mem.command_dispatcher_task_queue);

    if (command_dispatcher_command_queue_handle == NULL) {
        fatal("Failed to create command queue!\n");
    }
}

// Enqueue a command to be executed by the command dispatcher
void enqueue_command(command_t *p_cmd) {
    BaseType_t xStatus = xQueueSendToBack(command_dispatcher_command_queue_handle, p_cmd, 0);
    
    if (xStatus != pdPASS) {
        pvdx_task_t* calling_task = get_task(xTaskGetCurrentTaskHandle());
        fatal("%s task failed to enqueue command onto Command Dispatcher queue!\n", calling_task->name);
    }
}

// Forward a dequeued command to the appropriate task for execution
void dispatch_command(command_t cmd) {
    BaseType_t xStatus;

    switch (cmd.target) {
        case TASK_MANAGER:
            debug("command-dispatcher: popped task manager command\n");
            xStatus = xQueueSendToBack(task_manager_command_queue_handle, &cmd, 0);

            if (xStatus != pdPASS) {
                fatal("command-dispatcher: Failed to forward command to task manager task!\n");
            }

            break;
        case TASK_WATCHDOG:
            debug("command-dispatcher: popped watchdog command\n");
            xStatus = xQueueSendToBack(watchdog_command_queue_handle, &cmd, 0);

            if (xStatus != pdPASS) {
                fatal("command-dispatcher: Failed to forward command to watchdog task!\n");
            } else {
                debug("command_dispatcher: Successfully enqueued a command to watchdog command queue\n");
            }

            break;
        case TASK_SHELL:
            break;
        case TASK_HEARTBEAT:
            warning("command-dispatcher: Heartbeat task does not accept commands!\n");
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
            debug("command-dispatcher: popped display command\n");
            xStatus = xQueueSendToBack(display_command_queue_handle, &cmd, 0);

            if (xStatus != pdPASS) {
                fatal("command-dispatcher: Failed to forward command to display task!\n");
            }

            break;
        default:
            fatal("command-dispatcher: Invalid target task!\n");
            break;
    }
}
