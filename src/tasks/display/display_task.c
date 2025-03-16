/**
 * display_helpers.c
 *
 * Driver for the SSD1362 OLED controller within a Midas Displays MDOB256064D1Y-YS display.
 *
 * Created: February 29, 2024
 * Authors: Tanish Makadia, Ignacio Blancas Rodriguez, Aidan Wang, Siddharta Laloux
 */

#include "display_task.h"
#include "display_driver.h"

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

/**
 * \fn display_image
 *
 * \brief Overwrites the display buffer with the given buffer and triggers an 
 * update of the display
 * 
 * \param p_buffer pointer to the image buffer to be displayed
 *
 * \returns `status_t` SUCCESS if the operation was successful, or an error code otherwise
 */
status_t display_image(const color_t *const p_buffer) {
    debug("display: Displaying new image\n");

    display_set_buffer(p_buffer);
    ret_err_status(display_update(), "display: Update failed");
    
    return SUCCESS;
}

/**
 * \fn clear_image
 *
 * \brief Clears the display buffer and triggers an update of the display
 *
 * \returns `status_t` SUCCESS if the operation was successful, or an error code otherwise
 */
status_t clear_image(void) {
    debug("display: Clearing currently displayed image\n");

    display_clear_buffer();
    ret_err_status(display_update(), "display: Update failed");

    return SUCCESS;
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn get_display_image_command
 * 
 * \brief Gets the task command for display image
 *
 * \param p_buffer the pointer to the buffer of what we want to display
 *
 * \returns `command_t`, the command to display image
 */
inline command_t get_display_image_command(const color_t *const p_buffer) {
    // NOTE: Be sure to use a pointer to a static lifetime variable to ensure
    // that `*p_data` is still valid when the command is received.
    command_t cmd = {.target = p_display_task,
                     .operation = OPERATION_DISPLAY_IMAGE,
                     .p_data = p_buffer,
                     .len = sizeof(color_t *),
                     .result = PROCESSING,
                     .callback = NULL};
    return cmd;
}

/**
 * \fn exec_command_display
 * 
 * \brief Calls the correct function for the command
 *
 * \param p_cmd the pointer to the cmd we want to execute
 *
 */
void exec_command_display(command_t *const p_cmd) {
    if (p_cmd->target != p_display_task) {
        fatal("display: command target is not display! target: %s operation: %d\n", p_cmd->target->name, p_cmd->operation);
    }

    switch (p_cmd->operation) {
        case OPERATION_DISPLAY_IMAGE:
            p_cmd->result = display_image((const color_t *)p_cmd->p_data);
            break;
        case OPERATION_CLEAR_IMAGE:
            p_cmd->result = clear_image();
            break;
        default:
            fatal("display: Invalid operation! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
            break;
    }
}

/**
 * \fn init_display
 * 
 * \brief Initializes the display task
 *
 * \returns the command queue for the display
 */
QueueHandle_t init_display(void) {
    // Initialize the display hardware
    status_t status = init_display_hardware();

    fatal_on_error(status, "Failed to initialize display hardware!\n");

    // Initialize the display command queue
    QueueHandle_t display_command_queue_handle = xQueueCreateStatic(
        COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, display_mem.display_command_queue_buffer, &display_mem.display_task_queue);
    if (display_command_queue_handle == NULL) {
        fatal("Failed to create display queue!\n");
    }

    return display_command_queue_handle;
}
