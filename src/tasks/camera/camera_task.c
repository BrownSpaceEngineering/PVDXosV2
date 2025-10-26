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

/**
 * \fn camera_capture_image
 *
 * \brief Captures an image using the ArduCam with optional configuration override
 *
 * \param image_buffer pointer to camera_image_t structure to fill
 * \param config optional configuration override (NULL to use current config)
 *
 * \returns status_t SUCCESS if capture was successful
 */
status_t camera_capture_image(camera_image_t *const image_buffer, const camera_config_t *const config) {
    if (!image_buffer) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    if (!camera_status.initialized) {
        warning("camera: Attempted to capture image before camera initialization\n");
        return ERROR_NOT_READY;
    }
    
    if (camera_status.capturing) {
        warning("camera: Attempted to capture image while another capture is in progress\n");
        return ERROR_NOT_READY;
    }
    
    const camera_config_t *capture_config = config ? config : &camera_config;
    uint32_t capture_start_time = xTaskGetTickCount();
    
    debug("camera: Starting image capture (resolution: %dx%d, format: %d)\n", 
          capture_config->width, capture_config->height, capture_config->format);
    
    camera_status.capturing = true;
    
    // Configure camera with capture settings
    ret_err_status(camera_configure_settings(capture_config), 
                   "camera: Failed to configure camera settings");
    
    // Start capture
    ret_err_status(camera_start_capture(capture_config), 
                   "camera: Failed to start capture");
    
    // Wait for capture completion and get image data
    status_t result = camera_hw_get_captured_image(image_buffer, CAMERA_HW_CAPTURE_TIMEOUT_MS);
    
    camera_status.capturing = false;
    
    if (result == SUCCESS) {
        uint32_t capture_time = (xTaskGetTickCount() - capture_start_time) / portTICK_PERIOD_MS;
        
        // Update image metadata
        image_buffer->timestamp = capture_start_time;
        image_buffer->width = capture_config->width;
        image_buffer->height = capture_config->height;
        image_buffer->format = capture_config->format;
        image_buffer->valid = true;
        
        // Update status
        camera_status.images_captured++;
        camera_status.last_capture_time = capture_start_time;
        
        debug("camera: Image captured successfully (size: %d bytes, time: %d ms)\n", 
              image_buffer->size, capture_time);
    } else {
        camera_status.capture_errors++;
        warning("camera: Image capture failed\n");
    }
    
    return result;
}

// Note: camera_configure is implemented in camera.c

// Note: camera_get_status is implemented in camera.c

/**
 * \fn camera_auto_exposure_calibrate
 *
 * \brief Performs auto-exposure calibration to optimize image brightness
 *
 * \param target_brightness target brightness level (0-255)
 * \param force_recalibration force recalibration even if current exposure is optimal
 *
 * \returns status_t SUCCESS if calibration was successful
 */
status_t camera_auto_exposure_calibrate(uint8_t target_brightness, bool force_recalibration) {
    if (!camera_status.initialized) {
        warning("camera: Attempted auto-exposure calibration before camera initialization\n");
        return ERROR_NOT_READY;
    }
    
    // Note: target_brightness is uint8_t, so it's always <= 255
    
    debug("camera: Starting auto-exposure calibration (target brightness: %d)\n", target_brightness);
    
    // TODO: Re-enable ae_analysis when camera_status_t structure is updated
    // camera_ae_analysis_t *ae = &camera_status.ae_analysis;
    uint8_t current_exposure = camera_config.exposure;
    uint8_t best_exposure = current_exposure;
    uint8_t best_brightness = 0;
    uint16_t min_deviation = 255;
    
    // Test multiple exposure values to find optimal setting
    for (uint8_t step = 0; step < CAMERA_MAX_EXPOSURE_STEPS; step++) {
        uint8_t test_exposure = CAMERA_AE_MIN_EXPOSURE + (step * CAMERA_AE_STEP_SIZE);
        
        if (test_exposure > CAMERA_AE_MAX_EXPOSURE) {
            test_exposure = CAMERA_AE_MAX_EXPOSURE;
        }
        
        // Temporarily set exposure
        camera_config.exposure = test_exposure;
        status_t exp_result = camera_set_exposure(test_exposure);
        if (exp_result != SUCCESS) {
            warning("camera: Failed to set test exposure\n");
            return exp_result;
        }
        
        // Take a test image
        camera_image_t test_image;
        status_t result = camera_capture_image(&test_image, NULL);
        
        if (result == SUCCESS) {
            uint16_t brightness;
            result = camera_analyze_brightness(&test_image, &brightness);
            
            if (result == SUCCESS) {
                uint16_t deviation = (brightness > target_brightness) ? 
                    (brightness - target_brightness) : (target_brightness - brightness);
                
                if (deviation < min_deviation) {
                    min_deviation = deviation;
                    best_exposure = test_exposure;
                    best_brightness = brightness;
                }
                
                // Store sample for analysis
                // TODO: Re-enable when camera_status_t structure is updated
                // if (step < CAMERA_AUTO_EXPOSURE_SAMPLES) {
                //     ae->brightness_samples[step] = brightness;
                // }
                
                debug("camera: Test exposure %d -> brightness %d (deviation: %d)\n", 
                      test_exposure, brightness, deviation);
            }
        }
        
        // Break if we found a good enough exposure
        if (min_deviation <= CAMERA_AE_TOLERANCE) {
            break;
        }
    }
    
    // Restore original exposure if calibration failed
    if (min_deviation > CAMERA_AE_TOLERANCE) {
        warning("camera: Auto-exposure calibration failed to find optimal setting\n");
        camera_config.exposure = current_exposure;
        camera_set_exposure(current_exposure);
        return ERROR_READ_FAILED;
    }
    
    // Apply optimal exposure
    camera_config.exposure = best_exposure;
    camera_status.current_config.exposure = best_exposure;
    status_t set_result = camera_hw_set_exposure(best_exposure);
    if (set_result != SUCCESS) {
        warning("camera: Failed to set optimal exposure\n");
        return set_result;
    }
    
    // Update analysis data
    // TODO: Re-enable when camera_status_t structure is updated
    // ae->average_brightness = best_brightness;
    // ae->current_exposure = best_exposure;
    // ae->recommended_exposure = best_exposure;
    // ae->exposure_optimal = true;
    
    info("camera: Auto-exposure calibration complete (exposure: %d -> %d, brightness: %d)\n", 
         current_exposure, best_exposure, best_brightness);
    
    return SUCCESS;
}

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
