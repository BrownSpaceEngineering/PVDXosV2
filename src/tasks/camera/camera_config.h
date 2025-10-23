/**
 * camera_config.h
 *
 * Camera system configuration constants and settings.
 * Centralized configuration management for the camera system.
 *
 * Created: January 24, 2025
 * Authors: PVDX Team
 */

#ifndef CAMERA_CONFIG_H
#define CAMERA_CONFIG_H

// ============================================================================
// CAMERA SYSTEM CONFIGURATION
// ============================================================================

// Task configuration
#define CAMERA_TASK_STACK_SIZE       2048    // Stack size in words
#define CAMERA_TASK_PRIORITY         2       // Task priority
#define CAMERA_TASK_NAME             "Camera"

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

// Hardware communication timeouts
#define CAMERA_HW_SPI_TIMEOUT_MS     1000              // SPI transaction timeout
#define CAMERA_HW_INIT_TIMEOUT_MS    5000              // Initialization timeout
#define CAMERA_HW_CAPTURE_TIMEOUT_MS 10000             // Capture timeout
#define CAMERA_HW_FIFO_FLUSH_TIMEOUT_MS 1000           // FIFO flush timeout

// Camera operation timeouts
#define CAMERA_OPERATION_TIMEOUT_MS   5000              // General operation timeout
#define CAMERA_CAPTURE_TIMEOUT_MS    10000             // Image capture timeout
#define CAMERA_CONFIG_TIMEOUT_MS     2000              // Configuration timeout

// Continuous capture configuration
#define CAMERA_CONTINUOUS_MIN_INTERVAL_MS  100         // Minimum interval between captures
#define CAMERA_CONTINUOUS_MAX_INTERVAL_MS  60000       // Maximum interval between captures
#define CAMERA_CONTINUOUS_DEFAULT_INTERVAL_MS 1000     // Default interval (1 second)

// Image processing configuration
#define CAMERA_IMAGE_MIN_WIDTH       64                // Minimum image width
#define CAMERA_IMAGE_MIN_HEIGHT      64                // Minimum image height
#define CAMERA_IMAGE_MAX_WIDTH       320               // Maximum image width
#define CAMERA_IMAGE_MAX_HEIGHT      240               // Maximum image height

// Buffer management configuration
#define CAMERA_BUFFER_TIMEOUT_MS     5000              // Buffer allocation timeout
#define CAMERA_BUFFER_CLEANUP_MS    1000              // Buffer cleanup interval

// Error handling configuration
#define CAMERA_MAX_RETRIES          3                  // Maximum retry attempts
#define CAMERA_RETRY_DELAY_MS       100                // Delay between retries
#define CAMERA_ERROR_THRESHOLD     5                  // Error threshold before status change

// Performance monitoring configuration
#define CAMERA_PERF_SAMPLES         10                 // Performance monitoring samples
#define CAMERA_PERF_WINDOW_MS       1000              // Performance monitoring window

// ============================================================================
// CAMERA HARDWARE CONFIGURATION
// ============================================================================

// ArduCam SPI configuration
#define CAMERA_HW_SPI_CS_PIN         Camera_CS
#define CAMERA_HW_SPI_INSTANCE       SPI_0
#define CAMERA_HW_SPI_FREQUENCY      1000000          // 1MHz SPI frequency

// ArduCam register addresses
#define CAMERA_HW_REG_SENSOR_ID      0x300A
#define CAMERA_HW_REG_CHIP_ID        0x300B
#define CAMERA_HW_REG_TIMING_CTRL    0x3820
#define CAMERA_HW_REG_EXPOSURE_H     0x3500
#define CAMERA_HW_REG_EXPOSURE_M     0x3501
#define CAMERA_HW_REG_EXPOSURE_L     0x3502
#define CAMERA_HW_REG_GAIN           0x350A
#define CAMERA_HW_REG_BRIGHTNESS     0x3507
#define CAMERA_HW_REG_CONTRAST       0x3508

// ArduCam SPI commands
#define CAMERA_HW_CMD_READ_REG       0x00
#define CAMERA_HW_CMD_WRITE_REG      0x01
#define CAMERA_HW_CMD_READ_FIFO      0x02
#define CAMERA_HW_CMD_CAPTURE        0x03
#define CAMERA_HW_CMD_FLUSH_FIFO     0x04
#define CAMERA_HW_CMD_SINGLE_CAPTURE 0x05
#define CAMERA_HW_CMD_CONTINUOUS_CAPTURE 0x06
#define CAMERA_HW_CMD_STOP_CAPTURE   0x07

// ArduCam status codes
#define CAMERA_HW_STATUS_OK          0x00
#define CAMERA_HW_STATUS_ERROR       0x01
#define CAMERA_HW_STATUS_BUSY        0x02
#define CAMERA_HW_STATUS_TIMEOUT     0x03

// ============================================================================
// CAMERA DEBUG CONFIGURATION
// ============================================================================

#ifdef DEVBUILD
    #define CAMERA_DEBUG_ENABLED     1
    #define CAMERA_DEBUG_VERBOSE     1
    #define CAMERA_DEBUG_PERFORMANCE 1
#else
    #define CAMERA_DEBUG_ENABLED     0
    #define CAMERA_DEBUG_VERBOSE     0
    #define CAMERA_DEBUG_PERFORMANCE 0
#endif

// Debug output macros
#if CAMERA_DEBUG_ENABLED
    #define CAMERA_DEBUG(fmt, ...)   debug("camera: " fmt, ##__VA_ARGS__)
    #define CAMERA_DEBUG_HW(fmt, ...) debug("camera_hw: " fmt, ##__VA_ARGS__)
#else
    #define CAMERA_DEBUG(fmt, ...)
    #define CAMERA_DEBUG_HW(fmt, ...)
#endif

#if CAMERA_DEBUG_VERBOSE
    #define CAMERA_DEBUG_VERBOSE(fmt, ...) debug("camera: " fmt, ##__VA_ARGS__)
#else
    #define CAMERA_DEBUG_VERBOSE(fmt, ...)
#endif

#if CAMERA_DEBUG_PERFORMANCE
    #define CAMERA_DEBUG_PERF(fmt, ...) debug("camera_perf: " fmt, ##__VA_ARGS__)
#else
    #define CAMERA_DEBUG_PERF(fmt, ...)
#endif

#endif // CAMERA_CONFIG_H
