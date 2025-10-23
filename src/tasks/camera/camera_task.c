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

// Global configuration
camera_config_t camera_config = {
    .width = CAMERA_DEFAULT_WIDTH,
    .height = CAMERA_DEFAULT_HEIGHT,
    .format = CAMERA_FORMAT_RGB565,
    .quality = CAMERA_DEFAULT_QUALITY,
    .exposure = CAMERA_DEFAULT_EXPOSURE,
    .brightness = CAMERA_DEFAULT_BRIGHTNESS,
    .contrast = CAMERA_DEFAULT_CONTRAST,
    .auto_exposure_enabled = true,
    .capture_mode = CAMERA_CAPTURE_SINGLE,
    .capture_interval_ms = 1000
};

// Global image buffers
camera_image_t camera_buffers[CAMERA_BUFFER_COUNT];

// Global camera status
camera_status_t camera_status = {
    .initialized = false,
    .capturing = false,
    .images_captured = 0,
    .capture_errors = 0,
    .last_capture_time = 0,
    .current_config = camera_config,
    .ae_analysis = {0}
};

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
    uint32_t capture_start_time = rtc_get_seconds();
    
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
        uint32_t capture_time = rtc_get_seconds() - capture_start_time;
        
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

/**
 * \fn camera_configure
 *
 * \brief Configures camera settings
 *
 * \param config pointer to camera configuration
 *
 * \returns status_t SUCCESS if configuration was successful
 */
status_t camera_configure(const camera_config_t *const config) {
    if (!config) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    debug("camera: Configuring camera settings\n");
    
    // Validate configuration parameters
    if (config->width == 0 || config->height == 0) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    if (config->exposure > 255 || config->brightness > 255 || config->contrast > 255) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    // Update global configuration
    camera_config = *config;
    camera_status.current_config = *config;
    
    // Apply configuration to hardware if camera is initialized
    if (camera_status.initialized) {
        ret_err_status(camera_configure_settings(config), 
                       "camera: Failed to apply configuration to hardware");
    }
    
    info("camera: Configuration updated successfully\n");
    return SUCCESS;
}

/**
 * \fn camera_get_status
 *
 * \brief Gets current camera status and statistics
 *
 * \param status pointer to camera_status_t structure to fill
 *
 * \returns status_t SUCCESS if status was retrieved successfully
 */
status_t camera_get_status(camera_status_t *const status) {
    if (!status) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    *status = camera_status;
    return SUCCESS;
}

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
    
    if (target_brightness > 255) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    debug("camera: Starting auto-exposure calibration (target brightness: %d)\n", target_brightness);
    
    camera_ae_analysis_t *ae = &camera_status.ae_analysis;
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
        ret_err_status(camera_set_exposure(test_exposure), 
                       "camera: Failed to set test exposure %d", test_exposure);
        
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
                if (step < CAMERA_AUTO_EXPOSURE_SAMPLES) {
                    ae->brightness_samples[step] = brightness;
                }
                
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
    ret_err_status(camera_hw_set_exposure(best_exposure), 
                   "camera: Failed to set optimal exposure %d", best_exposure);
    
    // Update analysis data
    ae->average_brightness = best_brightness;
    ae->current_exposure = best_exposure;
    ae->recommended_exposure = best_exposure;
    ae->exposure_optimal = true;
    
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
    ret_err_status(init_camera_hardware(), "camera: Hardware initialization failed");
    
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

/**
 * \fn camera_analyze_brightness
 *
 * \brief Analyzes image brightness for auto-exposure algorithm
 *
 * \param image pointer to image to analyze
 * \param brightness pointer to store calculated brightness
 *
 * \returns status_t SUCCESS if analysis was successful
 */
status_t camera_analyze_brightness(const camera_image_t *const image, uint16_t *brightness) {
    if (!image || !brightness || !image->valid) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    // Simple brightness calculation based on image format
    uint32_t total_brightness = 0;
    uint32_t pixel_count = 0;
    
    switch (image->format) {
        case RGB565: {
            uint16_t *pixels = (uint16_t*)image->data;
            uint32_t pixel_count_expected = (image->width * image->height);
            
            for (uint32_t i = 0; i < pixel_count_expected && i < (image->size / 2); i++) {
                uint16_t pixel = pixels[i];
                // Extract RGB components from RGB565
                uint8_t r = (pixel >> 11) & 0x1F;
                uint8_t g = (pixel >> 5) & 0x3F;
                uint8_t b = pixel & 0x1F;
                
                // Convert to 8-bit and calculate brightness
                r = (r * 255) / 31;
                g = (g * 255) / 63;
                b = (b * 255) / 31;
                
                total_brightness += (r + g + b) / 3;
                pixel_count++;
            }
            break;
        }
        
        case GRAYSCALE: {
            uint8_t *pixels = (uint8_t*)image->data;
            uint32_t pixel_count_expected = (image->width * image->height);
            
            for (uint32_t i = 0; i < pixel_count_expected && i < image->size; i++) {
                total_brightness += pixels[i];
                pixel_count++;
            }
            break;
        }
        
        default:
            warning("camera: Unsupported format for brightness analysis\n");
            return ERROR_NOT_READY;
    }
    
    if (pixel_count == 0) {
        return ERROR_READ_FAILED;
    }
    
    *brightness = total_brightness / pixel_count;
    return SUCCESS;
}

/**
 * \fn camera_adjust_exposure
 *
 * \brief Calculates new exposure value based on current and target brightness
 *
 * \param current_exposure current exposure setting
 * \param target_brightness target brightness level
 * \param current_brightness current brightness level
 * \param new_exposure pointer to store calculated exposure
 *
 * \returns status_t SUCCESS if calculation was successful
 */
status_t camera_adjust_exposure(uint8_t current_exposure, uint8_t target_brightness, 
                               uint8_t current_brightness, uint8_t *new_exposure) {
    if (!new_exposure) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    if (current_brightness == 0) {
        // Avoid division by zero
        *new_exposure = current_exposure + CAMERA_AE_STEP_SIZE;
        if (*new_exposure > CAMERA_AE_MAX_EXPOSURE) {
            *new_exposure = CAMERA_AE_MAX_EXPOSURE;
        }
        return SUCCESS;
    }
    
    // Calculate exposure adjustment based on brightness ratio
    uint16_t exposure_ratio = (target_brightness * 100) / current_brightness;
    uint8_t adjusted_exposure = (current_exposure * exposure_ratio) / 100;
    
    // Clamp to valid range
    if (adjusted_exposure < CAMERA_AE_MIN_EXPOSURE) {
        adjusted_exposure = CAMERA_AE_MIN_EXPOSURE;
    } else if (adjusted_exposure > CAMERA_AE_MAX_EXPOSURE) {
        adjusted_exposure = CAMERA_AE_MAX_EXPOSURE;
    }
    
    *new_exposure = adjusted_exposure;
    return SUCCESS;
}

/**
 * \fn camera_get_free_buffer
 *
 * \brief Gets a free image buffer from the buffer pool
 *
 * \returns camera_image_t* pointer to free buffer, or NULL if none available
 */
camera_image_t* camera_get_free_buffer(void) {
    for (int i = 0; i < CAMERA_BUFFER_COUNT; i++) {
        if (!camera_buffers[i].valid) {
            return &camera_buffers[i];
        }
    }
    return NULL;
}

/**
 * \fn camera_release_buffer
 *
 * \brief Releases an image buffer back to the pool
 *
 * \param buffer pointer to buffer to release
 */
void camera_release_buffer(camera_image_t *buffer) {
    if (buffer && buffer >= &camera_buffers[0] && buffer <= &camera_buffers[CAMERA_BUFFER_COUNT-1]) {
        buffer->valid = false;
        buffer->size = 0;
    }
}

/**
 * \fn camera_copy_image
 *
 * \brief Copies image data from source to destination buffer
 *
 * \param src pointer to source image
 * \param dst pointer to destination image
 *
 * \returns status_t SUCCESS if copy was successful
 */
status_t camera_copy_image(const camera_image_t *const src, camera_image_t *const dst) {
    if (!src || !dst || !src->valid) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    if (src->size > CAMERA_MAX_IMAGE_SIZE) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    // Copy image data
    memcpy(dst->data, src->data, src->size);
    
    // Copy metadata
    dst->size = src->size;
    dst->timestamp = src->timestamp;
    dst->width = src->width;
    dst->height = src->height;
    dst->format = src->format;
    dst->valid = true;
    
    return SUCCESS;
}

/* ---------- COMMAND CREATION FUNCTIONS ---------- */

/**
 * \fn get_camera_capture_command
 *
 * \brief Creates a command to capture an image
 *
 * \param image_buffer pointer to image buffer to fill
 * \param config optional capture configuration override
 *
 * \returns command_t camera capture command
 */
command_t camera_get_capture_command(camera_image_t *const image_buffer, const camera_config_t *const config) {
    camera_capture_args_t *args = malloc(sizeof(camera_capture_args_t));
    if (!args) {
        fatal("camera: Failed to allocate memory for capture command arguments\n");
    }
    
    args->image_buffer = image_buffer;
    args->capture_config = (camera_config_t*)config;  // Cast away const for command structure
    
    return (command_t){
        .target = p_camera_task,
        .operation = OPERATION_CAMERA_CAPTURE,
        .p_data = args,
        .len = sizeof(camera_capture_args_t),
        .result = NO_STATUS_RETURN,
        .callback = NULL
    };
}

/**
 * \fn get_camera_config_command
 *
 * \brief Creates a command to configure camera settings
 *
 * \param config pointer to camera configuration
 *
 * \returns command_t camera configuration command
 */
command_t camera_get_config_command(const camera_config_t *const config) {
    camera_config_args_t *args = malloc(sizeof(camera_config_args_t));
    if (!args) {
        fatal("camera: Failed to allocate memory for config command arguments\n");
    }
    
    args->config = (camera_config_t*)config;  // Cast away const for command structure
    
    return (command_t){
        .target = p_camera_task,
        .operation = OPERATION_CAMERA_CONFIG,
        .p_data = args,
        .len = sizeof(camera_config_args_t),
        .result = NO_STATUS_RETURN,
        .callback = NULL
    };
}

/**
 * \fn get_camera_status_command
 *
 * \brief Creates a command to get camera status
 *
 * \param status pointer to status buffer to fill
 *
 * \returns command_t camera status command
 */
command_t camera_get_status_command(camera_status_t *const status) {
    camera_status_args_t *args = malloc(sizeof(camera_status_args_t));
    if (!args) {
        fatal("camera: Failed to allocate memory for status command arguments\n");
    }
    
    args->status = status;
    
    return (command_t){
        .target = p_camera_task,
        .operation = OPERATION_CAMERA_STATUS,
        .p_data = args,
        .len = sizeof(camera_status_args_t),
        .result = NO_STATUS_RETURN,
        .callback = NULL
    };
}

/**
 * \fn get_camera_auto_exposure_command
 *
 * \brief Creates a command to perform auto-exposure calibration
 *
 * \param target_brightness target brightness level
 * \param force_recalibration force recalibration flag
 *
 * \returns command_t auto-exposure calibration command
 */
command_t camera_get_auto_exposure_command(uint8_t target_brightness, bool force_recalibration) {
    camera_auto_exposure_args_t *args = malloc(sizeof(camera_auto_exposure_args_t));
    if (!args) {
        fatal("camera: Failed to allocate memory for auto-exposure command arguments\n");
    }
    
    args->target_brightness = target_brightness;
    args->force_recalibration = force_recalibration;
    
    return (command_t){
        .target = p_camera_task,
        .operation = OPERATION_CAMERA_AUTO_EXPOSURE,
        .p_data = args,
        .len = sizeof(camera_auto_exposure_args_t),
        .result = NO_STATUS_RETURN,
        .callback = NULL
    };
}
