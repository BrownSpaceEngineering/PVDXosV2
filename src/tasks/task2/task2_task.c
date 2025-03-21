/**
 * task2_helpers.c
 *
 * Created: March 20, 2025
 * Authors:
 */

#include "task2_task.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

void execute_task2(command_t *const p_cmd) {
    info("task2: doing task!\n");
    info("task2: telling task1 that do_task is complete!\n");
    xTaskNotifyGive(p_cmd->source);
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn init_task2
 *
 * \brief Initialises task2 command queue, before `init_task_pointer()`.
 *
 * \returns QueueHandle_t, a handle to the created queue
 *
 * \see `init_task_pointer()` for usage of functions of the type `init_<TASK>()`
 */

 void exec_command_task2(command_t *const p_cmd) {
    if (p_cmd->target != p_test_two_task) {
        fatal("Not task2! target: %d operation: %d\n", p_cmd->target->name, p_cmd->operation);
    }
    execute_task2(p_cmd);
}

QueueHandle_t init_task2(void) {
    QueueHandle_t task2_command_queue_handle = xQueueCreateStatic(
        COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, task2_mem.task2_command_queue_buffer,
        &task2_mem.task2_task_queue);

    if (task2_command_queue_handle == NULL) {
        fatal("Failed to create command queue!\n");
    }

    return task2_command_queue_handle;
}