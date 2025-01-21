#include "display_task.h"

// Display Task memory structures
display_task_memory_t display_mem;
uint8_t display_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
QueueHandle_t display_command_queue_handle;

void main_display(void *pvParameters) {
    info("display: Started main loop\n");

    // Initialize the display
    init_display();
    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    command_t command_checkin = {TASK_WATCHDOG, OPERATION_CHECKIN, &handle, sizeof(TaskHandle_t*), NULL, NULL};

    // TODO: Receive commands from the command dispatcher task to update the display
    while (true) {
        display_set_buffer(IMAGE_BUFFER_PVDX);
        debug("display: First buffer set\n");
        display_update();
        debug("display: First image completed\n");
        vTaskDelay(pdMS_TO_TICKS(500));

        display_set_buffer(IMAGE_BUFFER_BROWNLOGO);
        debug("display: Second image buffer set\n");
        display_update();
        debug("display: Second image completed\n");
        vTaskDelay(pdMS_TO_TICKS(500));
        
        enqueue_command(&command_checkin);
    }
}