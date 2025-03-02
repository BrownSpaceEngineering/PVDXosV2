/**
 * test_two_helpers.c
 *
 * Created: February 24, 2025
 * Authors:
 */

#include "test_two_task.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

void handle_cmd_test_two(command_t *p_cmd) {
    debug("test_one received command \n");

    debug("test_one message received: %s \n", p_cmd->p_data);

    debug("test_one: end message processing\n");
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

// Initialize the command queue, which stores pointers to command structs
QueueHandle_t init_test_two(void) {
    QueueHandle_t test_two_command_queue_handle = xQueueCreateStatic(
        COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, test_two_mem.test_two_command_queue_buffer, &test_two_mem.test_two_task_queue);

    if (test_two_command_queue_handle == NULL) {
        fatal("Failed to create command queue!\n");
    }

    return test_two_command_queue_handle;
}