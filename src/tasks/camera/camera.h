/**
 * camera.h
 *
 * Main camera system public API.
 * Provides high-level camera functionality with consistent naming conventions.
 *
 * Created: January 24, 2025
 * Authors: PVDX Team
 */

#ifndef CAMERA_H
#define CAMERA_H

#include "../../globals.h"
#include "../../misc/logging/logging.h"
#include "../../ASF/thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/include/queue.h"
#include "../task_list.h"
#include "../../ASF/atmel_start.h"
#include "../watchdog/watchdog_task.h"
#include "camera_types.h"
#include "camera_config.h"
#include "camera_hw.h"
#include "camera_driver.h"

// ============================================================================
// CAMERA TASK CONFIGURATION
// ============================================================================

// Task memory structure (ensures TCB is placed higher than stack)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t camera_task_stack[CAMERA_TASK_STACK_SIZE];
    uint8_t camera_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t camera_task_queue;
    StaticTask_t camera_task_tcb;
} camera_task_memory_t;

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

extern camera_task_memory_t camera_mem;
extern camera_config_t camera_config;
extern camera_image_t camera_buffers[CAMERA_BUFFER_COUNT];
extern camera_status_t camera_status;

// ============================================================================
// CAMERA INITIALIZATION
// ============================================================================

/**
 * \brief Initialize camera system
 * \return QueueHandle_t Camera command queue handle
 */
QueueHandle_t camera_init(void);

/**
 * \brief Deinitialize camera system
 * \return status_t SUCCESS if deinitialization was successful
 */
status_t camera_deinit(void);

// ============================================================================
// CAMERA CORE FUNCTIONS
// ============================================================================

/**
 * \brief Capture a single image
 * \param image_buffer Pointer to image buffer to fill
 * \param config Optional configuration override (NULL to use current config)
 * \return status_t SUCCESS if capture was successful
 */
status_t camera_capture_image(camera_image_t *image_buffer, const camera_config_t *config);

/**
 * \brief Start continuous capture mode
 * \param interval_ms Interval between captures in milliseconds
 * \return status_t SUCCESS if continuous capture was started
 */
status_t camera_start_continuous(uint32_t interval_ms);

/**
 * \brief Stop continuous capture mode
 * \return status_t SUCCESS if continuous capture was stopped
 */
status_t camera_stop_continuous(void);

/**
 * \brief Configure camera settings
 * \param config Pointer to camera configuration
 * \return status_t SUCCESS if configuration was successful
 */
status_t camera_configure(const camera_config_t *config);

/**
 * \brief Get current camera status
 * \param status Pointer to status structure to fill
 * \return status_t SUCCESS if status was retrieved successfully
 */
status_t camera_get_status(camera_status_t *status);

/**
 * \brief Calibrate auto-exposure
 * \param target_brightness Target brightness level (0-255)
 * \param force_recalibration Force recalibration even if already calibrated
 * \return status_t SUCCESS if calibration was successful
 */
status_t camera_auto_exposure_calibrate(uint8_t target_brightness, bool force_recalibration);

// ============================================================================
// CAMERA IMAGE PROCESSING
// ============================================================================

/**
 * \brief Analyze image brightness for auto-exposure
 * \param image Pointer to image to analyze
 * \param brightness Pointer to store calculated brightness
 * \return status_t SUCCESS if analysis was successful
 */
status_t camera_analyze_brightness(const camera_image_t *image, uint16_t *brightness);

/**
 * \brief Adjust exposure based on brightness analysis
 * \param current_exposure Current exposure setting
 * \param target_brightness Target brightness level
 * \param current_brightness Current brightness level
 * \param new_exposure Pointer to store new exposure value
 * \return status_t SUCCESS if adjustment was successful
 */
status_t camera_adjust_exposure(uint8_t current_exposure, uint8_t target_brightness, 
                                uint8_t current_brightness, uint8_t *new_exposure);

// ============================================================================
// CAMERA BUFFER MANAGEMENT
// ============================================================================

/**
 * \brief Get a free image buffer
 * \return camera_image_t* Pointer to free buffer, or NULL if none available
 */
camera_image_t* camera_get_free_buffer(void);

/**
 * \brief Release an image buffer back to the pool
 * \param buffer Pointer to buffer to release
 */
void camera_release_buffer(camera_image_t *buffer);

/**
 * \brief Copy image data from source to destination
 * \param src Pointer to source image
 * \param dst Pointer to destination image
 * \return status_t SUCCESS if copy was successful
 */
status_t camera_copy_image(const camera_image_t *src, camera_image_t *dst);

// ============================================================================
// CAMERA TASK FUNCTIONS
// ============================================================================

/**
 * \brief Main camera task function
 * \param pvParameters Task parameters
 */
void camera_main(void *pvParameters);

/**
 * \brief Execute camera command
 * \param cmd Pointer to command to execute
 */
void camera_exec_command(command_t *cmd);

// ============================================================================
// CAMERA COMMAND CREATION
// ============================================================================

/**
 * \brief Create camera capture command
 * \param image_buffer Pointer to image buffer
 * \param config Optional configuration override
 * \return command_t Camera capture command
 */
command_t camera_get_capture_command(camera_image_t *image_buffer, const camera_config_t *config);

/**
 * \brief Create camera configuration command
 * \param config Pointer to configuration
 * \return command_t Camera configuration command
 */
command_t camera_get_config_command(const camera_config_t *config);

/**
 * \brief Create camera status command
 * \param status Pointer to status buffer
 * \return command_t Camera status command
 */
command_t camera_get_status_command(camera_status_t *status);

/**
 * \brief Create camera auto-exposure command
 * \param target_brightness Target brightness level
 * \param force_recalibration Force recalibration flag
 * \return command_t Camera auto-exposure command
 */
command_t camera_get_auto_exposure_command(uint8_t target_brightness, bool force_recalibration);

#endif // CAMERA_H
