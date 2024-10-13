#include "command_dispatcher_task.h"

// Initialize the command queue, which stores pointers to command structs
void command_dispatcher_init(void) {
    command_dispatcher_cmd_queue = xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, command_dispatcher_queue_buffer,
                                                    &command_dispatcher_mem.command_dispatcher_task_queue);

    if (command_dispatcher_cmd_queue == NULL) {
        fatal("command-dispatcher: Failed to create command queue!\n");
    }
}

// Enqueue a command to be executed by the command dispatcher
void command_dispatcher_enqueue(command_t cmd) {
    pvdx_task_t* calling_task = get_task(xTaskGetCurrentTaskHandle());

    BaseType_t xStatus = xQueueSendToBack(command_dispatcher_cmd_queue, &cmd, 0);

    if (xStatus != pdPASS) {
        fatal("command-dispatcher: %s task failed to enqueue command!\n", calling_task->name);
    }
}

// Forward a dequeued command to the appropriate task for execution
void command_dispatcher_exec(command_t cmd) {
    BaseType_t xStatus;

    switch (cmd.target) {
        case TASK_MANAGER:
            xStatus = xQueueSendToBack(task_manager_queue, &cmd, 0);

            if (xStatus != pdPASS) {
                fatal("command-dispatcher: Failed to forward command to task manager task!\n");
            }

            break;
        case TASK_SHELL:
            break;
        case TASK_HEARTBEAT:
            warning("command-dispatcher: Heartbeat task does not accept commands!\n");
            break;
        case TASK_MAGNETOMETER:
            fatal("command-dispatcher: Failed; Magnetometer queue does not exist");
            break;
        case TASK_CAMERA:
            fatal("command-dispatcher: Failed; Camera queue does not exist");
            break;
        case TASK_9AXIS:
            fatal("command-dispatcher: Failed; 9Axis queue does not exist");
            break;
        case TASK_DISPLAY:
            xStatus = xQueueSendToBack(displayQueue, &cmd, 0);

            if (xStatus != pdPASS) {
                fatal("command-dispatcher: Failed to forward command to display task!\n");
            }

            break;
        default:
            fatal("command-dispatcher: Invalid target task!\n");
            break;
    }

    // all subtask exec functions should only ever be called from the command dispatcher
}
