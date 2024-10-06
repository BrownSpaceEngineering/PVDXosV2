#include "command_executor_task.h"

struct CommandExecutorTaskMemory command_executor_mem;
uint8_t command_executor_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
QueueHandle_t command_executor_cmd_queue;

// Initialize the command queue, which stores pointers to command structs
void command_executor_init(void) {
    command_executor_cmd_queue = xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, command_executor_queue_buffer,
                                                    &command_executor_mem.command_executor_task_queue);

    if (command_executor_cmd_queue == NULL) {
        fatal("command-executor: Failed to create command queue!\n");
    }
}

// Enqueue a command to be executed by the command executor
void command_executor_enqueue(Command cmd) {
    PVDXTask* calling_task = get_task(xTaskGetCurrentTaskHandle());

    BaseType_t xStatus = xQueueSendToBack(command_executor_cmd_queue, &cmd, 0);

    if (xStatus != pdPASS) {
        fatal("command-executor: %s task failed to enqueue command!\n", calling_task->name);
    }
}

// Forward a dequeued command to the appropriate task for execution
void command_executor_exec(Command cmd) {
    BaseType_t xStatus;

    switch (cmd.target) {
        case TASK_MANAGER:
            xStatus = xQueueSendToBack(task_manager_queue, &cmd, 0);

            if (xStatus != pdPASS) {
                fatal("command-executor: Failed to forward command to task manager task!\n");
            }

            break;
        case TASK_SHELL:
            break;
        case TASK_HEARTBEAT:
            break;
        case TASK_MAGNETOMETER:
            fatal("command-executor: Failed; Magnetometer queue does not exist");
            break;
        case TASK_CAMERA:
            fatal("command-executor: Failed; Camera queue does not exist");
            break;
        case TASK_9AXIS:
            fatal("command-executor: Failed; 9Axis queue does not exist");
            break;
        case TASK_DISPLAY:
            xStatus = xQueueSendToBack(displayQueue, &cmd, 0);

            if (xStatus != pdPASS) {
                fatal("command-executor: Failed to forward command to display task!\n");
            }

            break;
        default:
            fatal("command-executor: Invalid target task!\n");
            break;
    }

    // all subtask exec functions should only ever be called from the command executor
}
