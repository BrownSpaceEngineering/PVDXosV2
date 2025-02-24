/**
 * test_one_helpers.c
 *
 * Created: February 24, 2025
 * Authors:
 */

#include "test_one_task.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

// NOTE: No dispatchable functions for the command dispatcher task. Its sole purpose is to
// forward commands to other tasks. Essentially, it's a glorified queue.

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

// Initialize the command queue, which stores pointers to command structs
QueueHandle_t init_test_one(void) {
    QueueHandle_t test_one_command_queue_handle = xQueueCreateStatic(
        COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, test_one_mem.test_one_command_queue_buffer,
        &test_one_mem.test_one_task_queue);

    if (test_one_command_queue_handle == NULL) {
        fatal("Failed to create command queue!\n");
    }

    return test_one_command_queue_handle;
}