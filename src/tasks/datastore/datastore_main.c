/**
 * datastore_main.c
 *
 * Main loop for the datastore task. This task is responsible for maintaining
 * the centralised sensor data storage system.
 *
 * Created: 06th February 2025
 * Authors: Siddharta Laloux
 */

#include "datastore_helpers.c"
#include "datastore_task.h"

datastore_task_memory_t datastore_mem;
uint8_t datastore_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
QueueHandle_t datastore_command_queue_handle;
uint8_t data_buffer[100];
mapping_t mapping_arr[10];

void main_datastore(void *pvParameters) {
    info("datastore: Task Started");

    // initialise
    status_t init_return = init_data_buffer(data_buffer, mapping_arr);

    while (true) {
        debug_impl("\n---------- Datastore Task Loop ----------\n");

        // TODO
    }
}
