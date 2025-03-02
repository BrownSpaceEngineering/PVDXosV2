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
    const command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time the command dispatcher should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;

    // Initialize the display command queue
    display_command_queue_handle =
        xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, display_command_queue_buffer, &display_mem.display_task_queue);
    if (display_command_queue_handle == NULL) {
        fatal("Failed to create display queue!\n");
    }

    // Initialize the display hardware
    status_t status = init_display();
    if (status != SUCCESS) {
        fatal("Failed to initialize display hardware!\n");
    }

    while (true) {
        debug_impl("\n---------- Display Task Loop ----------\n");

        (void)queue_block_time_ticks;
        // Execute all commands contained in the queue
        if (xQueueReceive(display_command_queue_handle, &cmd, 0) == pdPASS) {
            do {
                debug("display: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_display(&cmd);
            } while (xQueueReceive(display_command_queue_handle, &cmd, 0) == pdPASS);
        }
        debug("display: No more commands queued.\n");

        // Set the display buffer to the first image
        // status_t result = SUCCESS; // TODO: Don't initialize result to SUCCESS and block until it is set by the display_image command

        // {
        //     // TODO: Add logic for blocking on the result of the display_image command
        //     const command_t display_image_command = get_display_image_command(IMAGE_BUFFER_PVDX, &result);
        //     enqueue_command(&display_image_command);
        // }

        // if (result != SUCCESS) {
        //     warning("display: Failed to display image. Error code: %d\n", result);
        // }

        // {
        //     // Set the display buffer to the second image
        //     // TODO: Add logic for blocking on the result of the display_image command
        //     const command_t display_image_command = get_display_image_command(IMAGE_BUFFER_BROWNLOGO, &result);
        //     enqueue_command(&display_image_command);
        // }

        // if (result != SUCCESS) {
        //     warning("display: Failed to display image. Error code: %d\n", result);
        // }

        // Check in with the watchdog task
        enqueue_command(&cmd_checkin);
        debug("display: Enqueued watchdog checkin command\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
