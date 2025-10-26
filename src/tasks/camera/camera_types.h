/**
 * camera_types.h
 *
 * Camera system type definitions, enums, and structures.
 * Provides consistent naming and organization for all camera-related types.
 *
 * Created: January 24, 2025
 * Authors: PVDX Team
 */

#ifndef CAMERA_TYPES_H
#define CAMERA_TYPES_H

#include "../../globals.h"
#include "../../misc/logging/logging.h"
// #include "misc/rtc/rtc_driver.h" // RTC driver not available on this branch

// ============================================================================
// CAMERA CONSTANTS
// ============================================================================

// Task configuration
#define CAMERA_TASK_STACK_SIZE       2048    // Stack size in words
#define CAMERA_TASK_PRIORITY         2       // Task priority

// Image and buffer configuration
#define CAMERA_MAX_IMAGE_SIZE        (320 * 240 * 2)  // Max image size (RGB565)
#define CAMERA_BUFFER_COUNT          3                 // Number of image buffers
#define CAMERA_MAX_IMAGE_WIDTH       320               // Maximum image width
#define CAMERA_MAX_IMAGE_HEIGHT      240               // Maximum image height

// Default camera settings
#define CAMERA_DEFAULT_WIDTH         320
#define CAMERA_DEFAULT_HEIGHT        240
#define CAMERA_DEFAULT_EXPOSURE       100
#define CAMERA_DEFAULT_BRIGHTNESS     50
#define CAMERA_DEFAULT_CONTRAST       50
#define CAMERA_DEFAULT_QUALITY        CAMERA_QUALITY_MEDIUM
#define CAMERA_DEFAULT_FORMAT        CAMERA_FORMAT_RGB565

// Auto-exposure configuration
#define CAMERA_AE_SAMPLES            5                 // Number of samples for AE
#define CAMERA_AE_MAX_STEPS          10                // Maximum exposure steps
#define CAMERA_AE_TARGET_BRIGHTNESS  128               // Target brightness (0-255)
#define CAMERA_AE_TOLERANCE          10                // Acceptable deviation
#define CAMERA_AE_MIN_EXPOSURE       10                // Minimum exposure
#define CAMERA_AE_MAX_EXPOSURE       200               // Maximum exposure
#define CAMERA_AE_STEP_SIZE          20                // Exposure step size

// Hardware communication
#define CAMERA_SPI_TIMEOUT_MS        1000              // SPI transaction timeout
#define CAMERA_INIT_TIMEOUT_MS       5000              // Initialization timeout
#define CAMERA_CAPTURE_TIMEOUT_MS    10000             // Capture timeout

// ============================================================================
// CAMERA ENUMS
// ============================================================================

/**
 * Camera image format enumeration
 */
typedef enum {
    CAMERA_FORMAT_RGB565 = 0,        // 16-bit RGB565 format
    CAMERA_FORMAT_RGB888,            // 24-bit RGB888 format
    CAMERA_FORMAT_YUV422,            // YUV422 format
    CAMERA_FORMAT_JPEG,              // JPEG compressed format
    CAMERA_FORMAT_GRAYSCALE           // 8-bit grayscale format
} camera_format_t;

/**
 * Camera quality levels for JPEG compression
 */
typedef enum {
    CAMERA_QUALITY_LOW = 50,         // Low quality (50%)
    CAMERA_QUALITY_MEDIUM = 80,      // Medium quality (80%)
    CAMERA_QUALITY_HIGH = 95,        // High quality (95%)
    CAMERA_QUALITY_MAX = 100         // Maximum quality (100%)
} camera_quality_t;

/**
 * Camera capture modes
 */
typedef enum {
    CAMERA_CAPTURE_SINGLE = 0,       // Single image capture
    CAMERA_CAPTURE_CONTINUOUS,       // Continuous capture mode
    CAMERA_CAPTURE_BURST             // Burst capture mode
} camera_capture_mode_t;

/**
 * Camera status enumeration
 */
typedef enum {
    CAMERA_STATUS_UNINITIALIZED = 0, // Camera not initialized
    CAMERA_STATUS_IDLE,              // Camera ready for capture
    CAMERA_STATUS_CAPTURING,         // Camera currently capturing
    CAMERA_STATUS_ERROR,             // Camera in error state
    CAMERA_STATUS_BUSY               // Camera busy with operation
} camera_status_enum_t;

// ============================================================================
// CAMERA STRUCTURES
// ============================================================================

/**
 * Camera configuration structure
 */
typedef struct {
    uint16_t width;                          // Image width
    uint16_t height;                         // Image height
    camera_format_t format;                  // Image format
    camera_quality_t quality;                // JPEG quality (if applicable)
    uint8_t exposure;                        // Exposure level (0-255)
    uint8_t brightness;                      // Brightness level (0-255)
    uint8_t contrast;                        // Contrast level (0-255)
    bool auto_exposure_enabled;              // Enable auto-exposure
    camera_capture_mode_t capture_mode;      // Capture mode
    uint32_t capture_interval_ms;           // Interval for continuous mode
} camera_config_t;

/**
 * Camera image structure
 */
typedef struct {
    uint8_t data[CAMERA_MAX_IMAGE_SIZE];     // Image data buffer
    uint32_t size;                           // Actual image size in bytes
    uint32_t timestamp;                      // Capture timestamp (RTC seconds)
    uint16_t width;                          // Image width
    uint16_t height;                         // Image height
    camera_format_t format;                  // Image format
    bool valid;                              // Data validity flag
} camera_image_t;

/**
 * Camera status structure
 */
typedef struct {
    camera_status_enum_t status;             // Current camera status
    bool initialized;                        // Initialization status
    bool capturing;                         // Currently capturing flag
    uint32_t images_captured;               // Total images captured
    uint32_t capture_errors;                // Total capture errors
    uint32_t last_capture_time;             // Timestamp of last capture
    camera_config_t current_config;          // Current configuration
} camera_status_t;

/**
 * Camera capture result structure
 */
typedef struct {
    camera_image_t *image_buffer;            // Pointer to image buffer
    bool success;                            // Capture success flag
    uint32_t capture_time_ms;                // Time taken for capture
    uint8_t final_exposure;                 // Final exposure used
    bool auto_exposure_applied;             // Whether auto-exposure was applied
} camera_capture_result_t;

/**
 * Auto-exposure analysis structure
 */
typedef struct {
    uint16_t brightness_samples[CAMERA_AE_SAMPLES];  // Brightness samples
    uint8_t average_brightness;                      // Average brightness
    uint8_t current_exposure;                        // Current exposure setting
    uint8_t recommended_exposure;                    // Recommended exposure
    bool exposure_optimal;                           // Whether exposure is optimal
} camera_ae_analysis_t;

// ============================================================================
// CAMERA COMMAND ARGUMENTS
// ============================================================================

/**
 * Camera capture command arguments
 */
typedef struct {
    camera_image_t *image_buffer;            // Buffer to store captured image
    camera_config_t *capture_config;         // Optional configuration override
} camera_capture_args_t;

/**
 * Camera configuration command arguments
 */
typedef struct {
    camera_config_t *config;                 // New configuration to apply
} camera_config_args_t;

/**
 * Camera status command arguments
 */
typedef struct {
    camera_status_t *status;                 // Buffer to store camera status
} camera_status_args_t;

/**
 * Camera auto-exposure command arguments
 */
typedef struct {
    uint8_t target_brightness;               // Target brightness for auto-exposure
    bool force_recalibration;                // Force recalibration of auto-exposure
} camera_auto_exposure_args_t;

#endif // CAMERA_TYPES_H
