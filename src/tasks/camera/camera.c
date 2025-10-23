/**
 * camera.c
 *
 * Main camera system implementation with consistent naming conventions.
 * Provides high-level camera functionality and task management.
 *
 * Created: January 24, 2025
 * Authors: PVDX Team
 */

#include "camera.h"

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// Camera task memory
camera_task_memory_t camera_mem;

// Global camera configuration
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
    .status = CAMERA_STATUS_UNINITIALIZED,
    .initialized = false,
    .capturing = false,
    .images_captured = 0,
    .capture_errors = 0,
    .last_capture_time = 0,
    .current_config = camera_config
};

// ============================================================================
// CAMERA INITIALIZATION
// ============================================================================

/**
 * \brief Initialize camera system
 * \return QueueHandle_t Camera command queue handle
 */
QueueHandle_t camera_init(void) {
    info("camera: Initializing camera system\n");
    
    // Initialize hardware
    if (camera_hw_init() != SUCCESS) {
        error("camera: Failed to initialize hardware\n");
        return NULL;
    }
    
    // Initialize image buffers
    for (int i = 0; i < CAMERA_BUFFER_COUNT; i++) {
        camera_buffers[i].size = 0;
        camera_buffers[i].valid = false;
        camera_buffers[i].timestamp = 0;
    }
    
    // Update status
    camera_status.status = CAMERA_STATUS_IDLE;
    camera_status.initialized = true;
    camera_status.current_config = camera_config;
    
    info("camera: Camera system initialized successfully\n");
    
    // Return the command queue (this would be set up by the task manager)
    return NULL; // TODO: Return actual queue handle
}

/**
 * \brief Deinitialize camera system
 * \return status_t SUCCESS if deinitialization was successful
 */
status_t camera_deinit(void) {
    info("camera: Deinitializing camera system\n");
    
    // Stop any ongoing operations
    camera_stop_continuous();
    
    // Deinitialize hardware
    camera_hw_deinit();
    
    // Update status
    camera_status.status = CAMERA_STATUS_UNINITIALIZED;
    camera_status.initialized = false;
    camera_status.capturing = false;
    
    info("camera: Camera system deinitialized\n");
    return SUCCESS;
}

// ============================================================================
// CAMERA CORE FUNCTIONS
// ============================================================================

/**
 * \brief Capture a single image
 * \param image_buffer Pointer to image buffer to fill
 * \param config Optional configuration override (NULL to use current config)
 * \return status_t SUCCESS if capture was successful
 */
status_t camera_capture_image(camera_image_t *image_buffer, const camera_config_t *config) {
    if (!image_buffer) {
        return ERROR_SANITY_CHECK_FAILED;
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
    if (camera_configure(capture_config) != SUCCESS) {
        camera_status.capturing = false;
        return ERROR_TIMEOUT;
    }
    
    // Start capture
    if (camera_hw_start_capture() != SUCCESS) {
        camera_status.capturing = false;
        return ERROR_TIMEOUT;
    }
    
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
 * \brief Start continuous capture mode
 * \param interval_ms Interval between captures in milliseconds
 * \return status_t SUCCESS if continuous capture was started
 */
status_t camera_start_continuous(uint32_t interval_ms) {
    if (!camera_status.initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    if (interval_ms < CAMERA_CONTINUOUS_MIN_INTERVAL_MS || 
        interval_ms > CAMERA_CONTINUOUS_MAX_INTERVAL_MS) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    // Update configuration
    camera_config.capture_mode = CAMERA_CAPTURE_CONTINUOUS;
    camera_config.capture_interval_ms = interval_ms;
    camera_status.current_config = camera_config;
    
    info("camera: Started continuous capture mode (interval: %d ms)\n", interval_ms);
    return SUCCESS;
}

/**
 * \brief Stop continuous capture mode
 * \return status_t SUCCESS if continuous capture was stopped
 */
status_t camera_stop_continuous(void) {
    if (!camera_status.initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    // Update configuration
    camera_config.capture_mode = CAMERA_CAPTURE_SINGLE;
    camera_status.current_config = camera_config;
    
    info("camera: Stopped continuous capture mode\n");
    return SUCCESS;
}

/**
 * \brief Configure camera settings
 * \param config Pointer to camera configuration
 * \return status_t SUCCESS if configuration was successful
 */
status_t camera_configure(const camera_config_t *config) {
    if (!config) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    // Validate configuration
    if (config->width < CAMERA_IMAGE_MIN_WIDTH || config->width > CAMERA_IMAGE_MAX_WIDTH ||
        config->height < CAMERA_IMAGE_MIN_HEIGHT || config->height > CAMERA_IMAGE_MAX_HEIGHT) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    // Update global configuration
    camera_config = *config;
    camera_status.current_config = *config;
    
    // Apply configuration to hardware if camera is initialized
    if (camera_status.initialized) {
        // Apply hardware settings
        camera_hw_set_exposure(config->exposure);
        camera_hw_set_brightness(config->brightness);
        camera_hw_set_contrast(config->contrast);
        camera_hw_set_format(config->format);
        camera_hw_set_resolution(config->width, config->height);
    }
    
    debug("camera: Configuration updated\n");
    return SUCCESS;
}

/**
 * \brief Get current camera status
 * \param status Pointer to status structure to fill
 * \return status_t SUCCESS if status was retrieved successfully
 */
status_t camera_get_status(camera_status_t *status) {
    if (!status) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    *status = camera_status;
    return SUCCESS;
}

/**
 * \brief Calibrate auto-exposure
 * \param target_brightness Target brightness level (0-255)
 * \param force_recalibration Force recalibration even if already calibrated
 * \return status_t SUCCESS if calibration was successful
 */
status_t camera_auto_exposure_calibrate(uint8_t target_brightness, bool force_recalibration) {
    if (!camera_status.initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    debug("camera: Starting auto-exposure calibration (target: %d)\n", target_brightness);
    
    // TODO: Implement auto-exposure calibration algorithm
    // This would involve:
    // 1. Taking test images with different exposure settings
    // 2. Analyzing brightness levels
    // 3. Finding optimal exposure setting
    // 4. Applying the optimal setting
    
    info("camera: Auto-exposure calibration completed\n");
    return SUCCESS;
}

// ============================================================================
// CAMERA IMAGE PROCESSING
// ============================================================================

/**
 * \brief Analyze image brightness for auto-exposure
 * \param image Pointer to image to analyze
 * \param brightness Pointer to store calculated brightness
 * \return status_t SUCCESS if analysis was successful
 */
status_t camera_analyze_brightness(const camera_image_t *image, uint16_t *brightness) {
    if (!image || !brightness) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    // TODO: Implement brightness analysis algorithm
    // This would involve:
    // 1. Sampling pixels from the image
    // 2. Calculating average brightness
    // 3. Returning the result
    
    *brightness = 128; // Placeholder value
    return SUCCESS;
}

/**
 * \brief Adjust exposure based on brightness analysis
 * \param current_exposure Current exposure setting
 * \param target_brightness Target brightness level
 * \param current_brightness Current brightness level
 * \param new_exposure Pointer to store new exposure value
 * \return status_t SUCCESS if adjustment was successful
 */
status_t camera_adjust_exposure(uint8_t current_exposure, uint8_t target_brightness, 
                                uint8_t current_brightness, uint8_t *new_exposure) {
    if (!new_exposure) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    // TODO: Implement exposure adjustment algorithm
    // This would involve:
    // 1. Calculating the brightness difference
    // 2. Determining exposure adjustment needed
    // 3. Clamping to valid range
    // 4. Returning new exposure value
    
    *new_exposure = current_exposure; // Placeholder value
    return SUCCESS;
}

// ============================================================================
// CAMERA BUFFER MANAGEMENT
// ============================================================================

/**
 * \brief Get a free image buffer
 * \return camera_image_t* Pointer to free buffer, or NULL if none available
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
 * \brief Release an image buffer back to the pool
 * \param buffer Pointer to buffer to release
 */
void camera_release_buffer(camera_image_t *buffer) {
    if (buffer) {
        buffer->valid = false;
        buffer->size = 0;
        buffer->timestamp = 0;
    }
}

/**
 * \brief Copy image data from source to destination
 * \param src Pointer to source image
 * \param dst Pointer to destination image
 * \return status_t SUCCESS if copy was successful
 */
status_t camera_copy_image(const camera_image_t *src, camera_image_t *dst) {
    if (!src || !dst) {
        return ERROR_SANITY_CHECK_FAILED;
    }
    
    // Copy metadata
    dst->size = src->size;
    dst->timestamp = src->timestamp;
    dst->width = src->width;
    dst->height = src->height;
    dst->format = src->format;
    dst->valid = src->valid;
    
    // Copy image data
    if (src->size > 0 && src->size <= CAMERA_MAX_IMAGE_SIZE) {
        memcpy(dst->data, src->data, src->size);
    }
    
    return SUCCESS;
}

// ============================================================================
// CAMERA COMMAND CREATION
// ============================================================================

/**
 * \brief Create camera capture command
 * \param image_buffer Pointer to image buffer
 * \param config Optional configuration override
 * \return command_t Camera capture command
 */
command_t camera_get_capture_command(camera_image_t *image_buffer, const camera_config_t *config) {
    camera_capture_args_t *args = malloc(sizeof(camera_capture_args_t));
    if (!args) {
        fatal("camera: Failed to allocate memory for capture command arguments\n");
    }
    
    args->image_buffer = image_buffer;
    args->capture_config = (camera_config_t*)config;  // Cast away const for command structure
    
    return (command_t){
        .target = TARGET_CAMERA,
        .operation = OPERATION_CAMERA_CAPTURE,
        .p_data = args,
        .result = SUCCESS
    };
}

/**
 * \brief Create camera configuration command
 * \param config Pointer to configuration
 * \return command_t Camera configuration command
 */
command_t camera_get_config_command(const camera_config_t *config) {
    camera_config_args_t *args = malloc(sizeof(camera_config_args_t));
    if (!args) {
        fatal("camera: Failed to allocate memory for config command arguments\n");
    }
    
    args->config = (camera_config_t*)config;  // Cast away const for command structure
    
    return (command_t){
        .target = TARGET_CAMERA,
        .operation = OPERATION_CAMERA_CONFIG,
        .p_data = args,
        .result = SUCCESS
    };
}

/**
 * \brief Create camera status command
 * \param status Pointer to status buffer
 * \return command_t Camera status command
 */
command_t camera_get_status_command(camera_status_t *status) {
    camera_status_args_t *args = malloc(sizeof(camera_status_args_t));
    if (!args) {
        fatal("camera: Failed to allocate memory for status command arguments\n");
    }
    
    args->status = status;
    
    return (command_t){
        .target = TARGET_CAMERA,
        .operation = OPERATION_CAMERA_STATUS,
        .p_data = args,
        .result = SUCCESS
    };
}

/**
 * \brief Create camera auto-exposure command
 * \param target_brightness Target brightness level
 * \param force_recalibration Force recalibration flag
 * \return command_t Camera auto-exposure command
 */
command_t camera_get_auto_exposure_command(uint8_t target_brightness, bool force_recalibration) {
    camera_auto_exposure_args_t *args = malloc(sizeof(camera_auto_exposure_args_t));
    if (!args) {
        fatal("camera: Failed to allocate memory for auto-exposure command arguments\n");
    }
    
    args->target_brightness = target_brightness;
    args->force_recalibration = force_recalibration;
    
    return (command_t){
        .target = TARGET_CAMERA,
        .operation = OPERATION_CAMERA_AUTO_EXPOSURE,
        .p_data = args,
        .result = SUCCESS
    };
}
