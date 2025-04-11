/**
 * task1_helpers.c
 *
 * Created: March 20, 2025
 * Authors:
 */

#include "task1_task.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */
void execute_task1(void) {
    info("task1: doing task!\n");

    info("sending command to task2:");
    command_t cmd = {p_test_two_task, p_test_one_task, -1, NULL, 0, true, PROCESSING, NULL};
    enqueue_command(&cmd);

    info("task1: blocked waiting for notification!");
    /* Wait to be notified that the transmission is complete. Note
       the first parameter is pdTRUE, which has the effect of clearing
       the task's notification value back to 0, making the notification
       value act like a binary (rather than a counting) semaphore. */
    const TickType_t xMaxBlockTime = pdMS_TO_TICKS(5000);
    const uint32_t ulNotificationValue = ulTaskNotifyTake(pdTRUE, xMaxBlockTime);
    if (ulNotificationValue == 1) {
        info("task1: received notification!");
    }
    else {
        fatal("task1: did NOT recieve notification!");
    }
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

void exec_command_task1(command_t *const p_cmd) {
    if (p_cmd->target != p_test_one_task) {
        fatal("Not task1! target: %d operation: %d\n", p_cmd->target->name, p_cmd->operation);
    }
    execute_task1();
}

/**
 * \fn init_task1
 *
 * \brief Initialises task1 command queue, before `init_task_pointer()`.
 *
 * \returns QueueHandle_t, a handle to the created queue
 *
 * \see `init_task_pointer()` for usage of functions of the type `init_<TASK>()`
 */
QueueHandle_t init_task1(void) {
    QueueHandle_t task1_command_queue_handle = xQueueCreateStatic(
        COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, task1_mem.task1_command_queue_buffer,
        &task1_mem.task1_task_queue);

    if (task1_command_queue_handle == NULL) {
        fatal("Failed to create command queue!\n");
    }

    return task1_command_queue_handle;
}