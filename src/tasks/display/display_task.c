/**
 * display_task.c
 *
 * RTOS task wrapping the driver for a SSD1362 OLED controller within a Midas Displays
 * MDOB256064D1Y-YS display.
 *
 * Created: February 29, 2024
 * Authors: Tanish Makadia, Ignacio Blancas Rodriguez, Aidan Wang, Siddharta Laloux, Zach Mahan
 */

#include "display_task.h"

#include "camera_task.h"
#include "cmd_dispatcher.h"
#include "display_driver.h"
#include "state_machine.h"

// The state machine inputs the display task owns
static input_update_data_t display_inputs = {
    .input_update_bitmask = INPUT_UPDATE_BIT(PHOTO_READY_DOWNLINK_UPDATE),
};

/* ---------- PHOTO SEQUENCE ----------
 * display image -> camera capture -> display off -> photo_taken_for_downlink input update
 */

/**
 * \fn request_photo
 *
 * \brief Asks the camera to photograph what is currently on the display
 */
static void request_photo(void) {
    command_t cmd = get_camera_capture_command();
    const status_t status = enqueue_command(&cmd);
    if (status != SUCCESS) {
        warning("display: Failed to request a photo from the camera (status %d)\n", status);
    }
}

/**
 * \fn report_photo_taken
 *
 * \brief Tells the state machine that a photo is ready for downlink
 */
static void report_photo_taken(void) {
    display_inputs.new_values.photo_taken_for_downlink = true;
    command_t cmd = get_state_input_update_command(&display_inputs);
    const status_t status = enqueue_command(&cmd);
    if (status != SUCCESS) {
        warning("display: Failed to report photo taken to the state machine (status %d)\n", status);
    }
}

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
    command_t cmd = {
        .target = p_display_task,
        .operation = OPERATION_DISPLAY_IMAGE,
        .data.display_data = p_buffer,
        .data_type = CMD_DATA_DISPLAY,
        .result = PROCESSING,
    };
    return cmd;
}

/**
 * \fn get_display_init_command
 *
 * \brief Gets the task command to (re)initialise the display hardware
 *
 * \returns `command_t`, the command to initialise the display
 */
command_t get_display_init_command(void) {
    return (command_t){
        .target = p_display_task,
        .operation = OPERATION_DISPLAY_INIT,
        .data = {0},
        .data_type = CMD_DATA_NONE,
        .result = PROCESSING,
    };
}

/**
 * \brief Get a command to put the display to sleep, preserving its image memory.
 */
command_t get_display_off_command(void) {
    return (command_t){
        .target = p_display_task,
        .operation = OPERATION_POWER_OFF,
        .data = {0},
        .data_type = CMD_DATA_NONE,
        .result = PROCESSING,
    };
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
        case OPERATION_POWER_OFF:
            p_cmd->result = display_off();
            if (p_cmd->result == SUCCESS) {
                // The camera turns the display off once it has taken the photo
                report_photo_taken();
            }
            break;
        case OPERATION_DISPLAY_INIT:
            p_cmd->result = init_display_hardware();
            break;
        case OPERATION_DISPLAY_IMAGE:
            p_cmd->result = display_image((const color_t *)p_cmd->data.display_data);
            if (p_cmd->result == SUCCESS) {
                // Photograph the image now that it is on the display
                request_photo();
            }
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
    // Initialize the display command queue
    QueueHandle_t display_command_queue_handle = xQueueCreateStatic(
        COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, display_mem.display_command_queue_buffer, &display_mem.display_task_queue);
    if (display_command_queue_handle == NULL) {
        fatal("Failed to create display queue!\n");
    }

    return display_command_queue_handle;
}
