#include "camera_task.h"

#include "SEGGER_RTT.h"
#include "cmd_dispatcher.h"
#include "display_task.h"
#include "drivers/arducam/arducam_driver.h"
#include "logging.h"
#include "semphr.h"
#include "task_list.h"

static volatile bool stop_requested = false;
static StaticSemaphore_t stop_ack_storage;
static SemaphoreHandle_t stop_ack;
static bool camera_ready = false;
static bool image_channel_ready = false;
static uint8_t camera_rtt_image_buffer[SEGGER_RTT_IMAGE_BUFFER_SIZE];

QueueHandle_t init_camera(void) {
    stop_ack = xSemaphoreCreateBinaryStatic(&stop_ack_storage);
    if (stop_ack == NULL) {
        fatal("camera: Failed to create lifecycle acknowledgement semaphore!\n");
    }
    QueueHandle_t queue = xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, camera_mem.camera_command_queue_buffer,
                                             &camera_mem.camera_task_queue);
    if (queue == NULL) {
        fatal("camera: Failed to create command queue!\n");
    }
    return queue;
}

bool camera_stop_requested(void) {
    return stop_requested;
}

bool camera_prepare_suspend(void) {
    if (stop_ack == NULL) {
        return false;
    }
    // Drain the previous handshake before publishing a new request.
    (void)xSemaphoreTake(stop_ack, 0);
    stop_requested = true;
    return xSemaphoreTake(stop_ack, pdMS_TO_TICKS(3000)) == pdTRUE;
}

void camera_prepare_resume(void) {
    // Drop late queue writes from producers that passed admission before disable.
    // The manager still has admission closed and has not resumed the camera yet.
    xQueueReset(p_camera_task->command_queue);
    stop_requested = false;
}

void camera_park_if_stopped(void) {
    if (!camera_stop_requested()) {
        return;
    }

    // Only called by main_camera, after initialization/capture and driver cleanup return.
    xQueueReset(p_camera_task->command_queue);
    xSemaphoreGive(stop_ack);
    // Never self-suspend: the manager could resume us just before a self-suspend.
    while (camera_stop_requested()) {
        vTaskDelay(1);
    }
}

command_t get_camera_capture_command(void) {
    return (command_t){
        .target = p_camera_task,
        .operation = OPERATION_CAMERA_CAPTURE,
        .data = {0},
        .data_type = CMD_DATA_NONE,
        .result = PROCESSING,
    };
}

// Called only by the camera task, after the scheduler starts. Retry hardware init on
// the next capture request after a failure, without touching hardware while disabled.
status_t camera_capture(void) {
    if (camera_stop_requested()) {
        return ERROR_TASK_DISABLED;
    }
    if (!image_channel_ready) {
        if (SEGGER_RTT_ConfigUpBuffer(CAMERA_RTT_OUTPUT_CHANNEL, "Image", camera_rtt_image_buffer, sizeof(camera_rtt_image_buffer),
                                      SEGGER_RTT_MODE_NO_BLOCK_SKIP) != 0) {
            warning("camera: Failed to configure RTT image channel\n");
            return ERROR_NOT_READY;
        }
        image_channel_ready = true;
    }

    if (camera_stop_requested()) {
        return ERROR_TASK_DISABLED;
    }
    if (!camera_ready) {
        status_t status = init_arducam_hardware();
        if (camera_stop_requested()) {
            camera_ready = status == SUCCESS;
            return ERROR_TASK_DISABLED;
        }
        if (status != SUCCESS) {
            warning("camera: Hardware initialization failed (status=%d)\n", status);
            return status;
        }
        camera_ready = true;
    }

    if (camera_stop_requested()) {
        return ERROR_TASK_DISABLED;
    }
    status_t status = capture_rtt();
    if (status != SUCCESS) {
        if (status != ERROR_TASK_DISABLED) {
            warning("camera: Capture failed (status=%d)\n", status);
        }
        camera_ready = false;
    }
    return camera_stop_requested() ? ERROR_TASK_DISABLED : status;
}

void exec_command_camera(command_t *const p_cmd) {
    if (p_cmd->target != p_camera_task) {
        warning("camera: Command addressed to another task\n");
        p_cmd->result = ERROR_BAD_TARGET;
        return;
    }

    switch (p_cmd->operation) {
        case OPERATION_CAMERA_CAPTURE:
            if (camera_stop_requested()) {
                p_cmd->result = ERROR_TASK_DISABLED;
                break;
            }
            if (p_cmd->data_type != CMD_DATA_NONE) {
                p_cmd->result = ERROR_BAD_ARGS;
                break;
            }
            p_cmd->result = camera_capture();
            {
                // The photo is of the display, so turn it off whether or not the capture worked
                command_t display_off_cmd = get_display_off_command();
                const status_t status = enqueue_command(&display_off_cmd);
                if (status != SUCCESS) {
                    warning("camera: Failed to turn off the display after capture (status %d)\n", status);
                }
            }
            break;
        default:
            warning("camera: Unsupported operation %d\n", p_cmd->operation);
            p_cmd->result = ERROR_BAD_ARGS;
            break;
    }
}
