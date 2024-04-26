#include "command_executor_task.h"

struct commandExecutorTaskMemory commandExecutorMem;
uint8_t commandExecutorQueueBuffer[MAX_COMMANDS * QUEUE_ITEM_SIZE];
QueueHandle_t commandQueue;

// Initialize the command queue, which stores pointers to command structs
void command_executor_init(void) {
    commandQueue = xQueueCreateStatic(MAX_COMMANDS, QUEUE_ITEM_SIZE, commandExecutorQueueBuffer, &commandExecutorMem.commandExecutorTaskQueue);

    if (commandQueue == NULL) {
        fatal("command-executor: Failed to create command queue!\n");
    }
}

// Enqueue a pointer to a command that will be executed by the command executor
void command_executor_enqueue(cmd_t* p_cmd) {
    PVDXTask_t callingTask = task_manager_get_task(xTaskGetCurrentTaskHandle());

    BaseType_t xStatus = xQueueSendToBack(commandQueue, &p_cmd, 0);

    if (xStatus != pdPASS) {
        fatal("command-executor: %s task failed to enqueue command!\n", callingTask.name);
    }
}

// Forward a dequeued command to the appropriate task for execution
void command_executor_exec(cmd_t* p_cmd) {
    switch (p_cmd->target) {
        case TASK_DISPLAY:
            display_exec(p_cmd);
            break;
        default:
            fatal("command-executor: Invalid target task!\n");
            break;
    }

    // all subtask exec functions should only ever be called from the command executor
}
