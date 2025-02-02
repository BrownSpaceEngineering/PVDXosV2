/**
 * display_main.c
 *
 * Main loop of the Display task which controls the OLED display on PVDX.
 *
 * Created: February 29, 2024
 * Authors: Tanish Makadia, Ignacio Blancas Rodriguez
 */

#include "display_task.h"

// Display Task memory structures
display_task_memory_t display_mem;
uint8_t display_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
QueueHandle_t display_command_queue_handle;

void main_display(void *pvParameters) {
    info("display: Task Started!\n");

    // Cache the watchdog checkin command to avoid creating it every iteration
    const command_t cmd_checkin = get_watchdog_checkin_command();
    // Initialize the display
    init_display();

    // TODO: Receive commands from the command dispatcher task to update the display
    while (true) {
        debug_impl("\n---------- Display Task Loop ----------\n");

        // Set the display buffer to the first image
        display_set_buffer(IMAGE_BUFFER_PVDX);
        debug("display: First buffer set\n");
        display_update();
        debug("display: First image completed\n");
        vTaskDelay(pdMS_TO_TICKS(500));

        // Set the display buffer to the second image
        display_set_buffer(IMAGE_BUFFER_BROWNLOGO);
        debug("display: Second image buffer set\n");
        display_update();
        debug("display: Second image completed\n");
        vTaskDelay(pdMS_TO_TICKS(500));

        // Check in with the watchdog task
        enqueue_command(&cmd_checkin);
        debug("display: Enqueued watchdog checkin command\n");
    }
}
