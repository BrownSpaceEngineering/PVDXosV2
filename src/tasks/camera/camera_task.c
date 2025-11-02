/**
 * camera_task.c
 *
 * RTOS task for ArduCam camera module with comprehensive functionality including
 * image capture, auto-exposure algorithms, and configurable settings.
 *
 * Created: January 24, 2025
 * Authors: PVDX Team
 */

#include "camera.h"
#include "FreeRTOS.h"
#include "task.h"

// Note: Global variables (camera_config, camera_buffers, camera_status, camera_mem) 
// are defined in camera.c

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

// Note: camera_capture_image is implemented in camera.c
// Note: camera_configure is implemented in camera.c
// Note: camera_get_status is implemented in camera.c
// Note: camera_auto_exposure_calibrate is implemented in camera.c

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn init_camera
 *
 * \brief Initializes camera hardware and command queue
 *
 * \returns QueueHandle_t, a handle to the created queue
 */
QueueHandle_t init_camera(void) {
    debug("camera: Initializing camera task\n");
    
    // Initialize camera hardware
    status_t init_result = init_camera_hardware();
    if (init_result != SUCCESS) {
        warning("camera: Hardware initialization failed\n");
        return NULL;
    }
    
    // Create camera command queue
    QueueHandle_t camera_command_queue_handle = xQueueCreateStatic(
        COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, 
        camera_mem.camera_command_queue_buffer, &camera_mem.camera_task_queue);
    
    if (camera_command_queue_handle == NULL) {
        fatal("Failed to create camera command queue!\n");
    }
    
    // Initialize image buffers
    for (int i = 0; i < CAMERA_BUFFER_COUNT; i++) {
        camera_buffers[i].size = 0;
        camera_buffers[i].valid = false;
        camera_buffers[i].timestamp = 0;
    }
    
    // Update status
    camera_status.initialized = true;
    camera_status.current_config = camera_config;
    
    info("camera: Task initialization complete\n");
    
    return camera_command_queue_handle;
}

// Note: camera_analyze_brightness is implemented in camera.c

// Note: camera_adjust_exposure is implemented in camera.c

// Note: camera_get_free_buffer, camera_release_buffer, and camera_copy_image
// are implemented in camera.c

// Note: All command creation functions are implemented in camera.c
