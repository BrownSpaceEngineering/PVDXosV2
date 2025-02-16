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

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_task(xTaskGetCurrentTaskHandle());
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Initialize the display
    init_display();

    while (true) {
        debug_impl("\n---------- Display Task Loop ----------\n");

        // Set the display buffer to the first image
        display_image(IMAGE_BUFFER_PVDX);
        vTaskDelay(pdMS_TO_TICKS(500));

        // Set the display buffer to the second image
        display_image(IMAGE_BUFFER_BROWNLOGO);
        vTaskDelay(pdMS_TO_TICKS(500));

        // Check in with the watchdog task
        enqueue_command(&cmd_checkin);
        debug("display: Enqueued watchdog checkin command\n");
    }
}
