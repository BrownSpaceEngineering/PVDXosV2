/**
 * datastore_helpers.case
 *
 * Helper functions for the Datastore Manager task. This task is responsible for
 * collecting readings from all core sensors into a centralised buffer. Upon
 * receiving a command from another task, the Datastore Manager will return
 * the most recent value read from the given sensor to the enqueuing task.
 *
 * Created: 06th February 2025
 * Authors: Siddharta Laloux, Sas Majumder
 */
#include "datastore_task.h"
#include "logging.h"
#include "task_list.h"

extern datastore_task_memory_t datastore_mem;
extern uint8_t datastore_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
extern QueueHandle_t datastore_command_queue_handle;
extern pvdx_task_t *task_list[];

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

status_t read_value(TaskHandle_t sensor) {
    // on hold until we figure out the type of tasks (task_t or TaskHandle_t or ...)
    return ERROR_NOT_YET_IMPLEMENTED;
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/* Initialisation functions*/

/**
 * void init_datastore(void)
 *
 * Initialises the command queue associated with the datastore. Command queue
 * stores pointers to command_t structs.
 *
 * TO BE CALLED ONLY IN main()!
 *
 * - Parametres: N/A
 *
 * - Returns: N/A
 *
 * - Errors:
 *      Fatal on failure
 */
void init_datastore(void) {
    info("Datastore command queue initialisation start");
    datastore_command_queue_handle = xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, datastore_command_queue_buffer,
                                                        &datastore_mem.datastore_task_queue);

    if (datastore_command_queue_handle == NULL) {
        fatal("Failed to create datastore!\n");
    }

    info("Datastore command queue initialisation complete");
}

/**
 * status_t init_data_buffer(void *buffer_start, task_mapping_t *mapping_array)
 *
 * Given the start of the data buffer, and the start of the mapping array,
 * initialises circular buffers for each sensor and a mapping from each sensor
 * to its circular array.
 *
 * - Parametres:
 *      buffer_start: void *, the start of the memory allocated for the data
 *                    buffer.
 *      mapping_array: task_mapping_t *, the array mapping task handles to
 *                     sensor buffer addresses.
 *
 * - Returns:
 *      a status_t enum, the
 */
status_t init_data_buffer(void *buffer_start, mapping_t mapping_arr[]) {
    info("Datastore buffer initialisation start");

    pvdx_task_t *curr_task = *task_list;
    uint8_t mapping_index = 0;
    void *buffer_current = buffer_start;

    while (curr_task != NULL) {
        if (curr_task->task_type == SENSOR) {
            // Assign mapping before initialising buffer
            mapping_arr[mapping_index] = {
                curr_task,
                buffer_current,
            };

            // because this creates a buffer at the given address and returns
            // the address of the next buffer.
            buffer_current = (void *)create_circular_array(buffer_current, curr_task->reading_size, curr_task->num_readings);
        }

        curr_task++;
    }

    info("Datastore buffer initialisation complete");

    return SUCCESS;
}