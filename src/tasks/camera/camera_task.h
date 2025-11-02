#ifndef CAMERA_TASK_H
#define CAMERA_TASK_H

// Includes
#include "../../globals.h"
#include "../../misc/logging/logging.h"
#include "../../ASF/thirdparty/RTOS/freertos/FreeRTOSV10.0.0/Source/include/queue.h"
#include "../task_list.h"
#include "../../ASF/atmel_start.h"
#include "../watchdog/watchdog_task.h"
#include "camera_driver.h"
// #include "misc/rtc/rtc_driver.h" // RTC driver not available on this branch

// Constants
#define CAMERA_TASK_STACK_SIZE 2048 // Size of the stack in words (multiply by 4 to get bytes)

// Camera system constants
#define CAMERA_MAX_IMAGE_SIZE (320 * 240 * 2)  // Maximum image size in bytes (RGB565)
#define CAMERA_BUFFER_COUNT 1                  // Number of image buffers (reduced from 3 to fit RAM)
#define CAMERA_AUTO_EXPOSURE_SAMPLES 5         // Number of samples for auto-exposure algorithm
#define CAMERA_MAX_EXPOSURE_STEPS 10           // Maximum exposure adjustment steps

// Camera configuration constants
#define CAMERA_DEFAULT_WIDTH 320
#define CAMERA_DEFAULT_HEIGHT 240
#define CAMERA_DEFAULT_FORMAT RGB565
#define CAMERA_DEFAULT_QUALITY 80
#define CAMERA_DEFAULT_EXPOSURE 100
#define CAMERA_DEFAULT_BRIGHTNESS 50
#define CAMERA_DEFAULT_CONTRAST 50

// Auto-exposure algorithm constants
#define CAMERA_AE_TARGET_BRIGHTNESS 128        // Target brightness level (0-255)
#define CAMERA_AE_TOLERANCE 10                 // Acceptable brightness deviation
#define CAMERA_AE_MIN_EXPOSURE 10              // Minimum exposure value
#define CAMERA_AE_MAX_EXPOSURE 200             // Maximum exposure value
#define CAMERA_AE_STEP_SIZE 20                 // Exposure adjustment step size

// Placed in a struct to ensure that the TCB is placed higher than the stack in memory
//^ This ensures that stack overflows do not corrupt the TCB (since the stack grows downwards)
typedef struct {
    StackType_t overflow_buffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t camera_task_stack[CAMERA_TASK_STACK_SIZE];
    uint8_t camera_command_queue_buffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
    StaticQueue_t camera_task_queue;
    StaticTask_t camera_task_tcb;
} camera_task_memory_t;

// Camera image format enumeration
typedef enum camera_format_e {
    RGB565 = 0,
    RGB888,
    YUV422,
    JPEG,
    GRAYSCALE
} camera_format_t;

// Camera quality levels
typedef enum {
    QUALITY_LOW = 50,
    QUALITY_MEDIUM = 80,
    QUALITY_HIGH = 95,
    QUALITY_MAX = 100
} camera_quality_t;

// Camera capture mode
typedef enum {
    CAPTURE_SINGLE = 0,
    CAPTURE_CONTINUOUS,
    CAPTURE_BURST
} camera_capture_mode_t;

// Camera configuration structure
typedef struct camera_config_s {
    uint16_t width;                    // Image width
    uint16_t height;                   // Image height
    camera_format_t format;            // Image format
    camera_quality_t quality;          // JPEG quality (if applicable)
    uint8_t exposure;                  // Exposure level (0-255)
    uint8_t brightness;                // Brightness level (0-255)
    uint8_t contrast;                  // Contrast level (0-255)
    bool auto_exposure_enabled;        // Enable auto-exposure algorithm
    camera_capture_mode_t capture_mode; // Capture mode
    uint32_t capture_interval_ms;      // Interval between captures (continuous mode)
} camera_config_t;

// Image buffer structure
typedef struct camera_image_s {
    uint8_t data[CAMERA_MAX_IMAGE_SIZE];  // Image data buffer
    uint32_t size;                        // Actual image size in bytes
    uint32_t timestamp;                   // Capture timestamp
    uint16_t width;                       // Image width
    uint16_t height;                      // Image height
    camera_format_t format;               // Image format
    bool valid;                           // Data validity flag
} camera_image_t;

// Camera capture result structure
typedef struct {
    camera_image_t *image_buffer;         // Pointer to image buffer
    bool success;                         // Capture success flag
    uint32_t capture_time_ms;             // Time taken for capture
    uint8_t final_exposure;               // Final exposure used
    bool auto_exposure_applied;           // Whether auto-exposure was applied
} camera_capture_result_t;

// Auto-exposure analysis structure
typedef struct {
    uint16_t brightness_samples[CAMERA_AUTO_EXPOSURE_SAMPLES];  // Brightness samples
    uint8_t average_brightness;                                 // Average brightness
    uint8_t current_exposure;                                   // Current exposure setting
    uint8_t recommended_exposure;                               // Recommended exposure
    bool exposure_optimal;                                      // Whether exposure is optimal
} camera_ae_analysis_t;

// Camera status structure
typedef struct {
    bool initialized;                     // Camera initialization status
    bool capturing;                       // Currently capturing flag
    uint32_t images_captured;             // Total images captured
    uint32_t capture_errors;              // Total capture errors
    uint32_t last_capture_time;           // Timestamp of last capture
    camera_config_t current_config;       // Current camera configuration
    camera_ae_analysis_t ae_analysis;     // Auto-exposure analysis data
} camera_status_t;

// Command argument structures
typedef struct {
    camera_image_t *image_buffer;         // Buffer to store captured image
    camera_config_t *capture_config;      // Optional capture configuration override
} camera_capture_args_t;

typedef struct {
    camera_config_t *config;              // New configuration to apply
} camera_config_args_t;

typedef struct {
    camera_status_t *status;              // Buffer to store camera status
} camera_status_args_t;

typedef struct {
    uint8_t target_brightness;            // Target brightness for auto-exposure
    bool force_recalibration;             // Force recalibration of auto-exposure
} camera_auto_exposure_args_t;

// Global memory and configuration
extern camera_task_memory_t camera_mem;
extern camera_config_t camera_config;
extern camera_image_t camera_buffers[CAMERA_BUFFER_COUNT];
extern camera_status_t camera_status;

// Function declarations
QueueHandle_t init_camera(void);
void main_camera(void *pvParameters);
void exec_command_camera(command_t *const p_cmd);

// Core camera functions
status_t camera_capture_image(camera_image_t *const image_buffer, const camera_config_t *const config);
status_t camera_configure(const camera_config_t *const config);
status_t camera_get_status(camera_status_t *const status);
status_t camera_auto_exposure_calibrate(uint8_t target_brightness, bool force_recalibration);

// Image processing functions
status_t camera_analyze_brightness(const camera_image_t *const image, uint16_t *brightness);
status_t camera_adjust_exposure(uint8_t current_exposure, uint8_t target_brightness, uint8_t current_brightness, uint8_t *new_exposure);

// Buffer management functions
camera_image_t* camera_get_free_buffer(void);
void camera_release_buffer(camera_image_t *buffer);
status_t camera_copy_image(const camera_image_t *const src, camera_image_t *const dst);

// Command creation functions
command_t get_camera_capture_command(camera_image_t *const image_buffer, const camera_config_t *const config);
command_t get_camera_config_command(const camera_config_t *const config);
command_t get_camera_status_command(camera_status_t *const status);
command_t get_camera_auto_exposure_command(uint8_t target_brightness, bool force_recalibration);

#endif // CAMERA_TASK_H
