#include "display_task.h"

// Display Task memory structures
display_task_memory_t display_mem;
uint8_t display_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
QueueHandle_t display_queue;

void display_main(void *pvParameters) {
    info("display: Task started!\n");

    // Initialize the display
    display_init();

    // TODO: Receive commands from the command dispatcher task to update the display
    while (true) {
        display_set_buffer(IMAGE_BUFFER_PVDX);
        debug("First buffer set\n");
        display_update();
        debug("First image completed\n");
        vTaskDelay(pdMS_TO_TICKS(500));

        display_set_buffer(IMAGE_BUFFER_BROWNLOGO);
        debug("Second image buffer set\n");
        display_update();
        debug("Second image completed\n");
        vTaskDelay(pdMS_TO_TICKS(500));
        watchdog_checkin();
    }
}