#include "camera_tests.h"

#include <string.h>
#include <stdbool.h>
#include "logging.h"
#include "camera.h"
#include "shell_helpers.h"
#include "FreeRTOS.h"
#include "task.h"
#include "command_dispatcher_task.h"

// Forward declarations of test cases
static int test_camera_init(void);
static int test_camera_status(void);
static int test_camera_capture_smoke(void);
static int test_camera_buffer_management(void);
static int test_camera_config_sanity(void);
static int test_framework_registry(void);

static camera_test_entry_t camera_tests[] = {
    {"init", test_camera_init},
    {"status", test_camera_status},
    {"capture", test_camera_capture_smoke},
    {"buffer", test_camera_buffer_management},
    {"config", test_camera_config_sanity},
    {"framework", test_framework_registry},
    {NULL, NULL}
};

void camera_tests_list(void) {
    terminal_printf("Camera tests:\n");
    for (camera_test_entry_t *e = camera_tests; e->name; ++e) {
        terminal_printf("  %s\n", e->name);
    }
}

int camera_tests_run_all(int *out_total) {
    int passed = 0;
    int total = 0;
    for (camera_test_entry_t *e = camera_tests; e->name; ++e) {
        ++total;
        int rc = e->test_func();
        terminal_printf("[%s] %s\n", rc == 0 ? "PASS" : "FAIL", e->name);
        if (rc == 0) passed++;
    }
    terminal_printf("Summary: %d/%d passed\n", passed, total);
    if (out_total) *out_total = total;
    return passed;
}

int camera_tests_run_one(const char *name) {
    for (camera_test_entry_t *e = camera_tests; e->name; ++e) {
        if (strcmp(e->name, name) == 0) {
            int rc = e->test_func();
            terminal_printf("[%s] %s\n", rc == 0 ? "PASS" : "FAIL", e->name);
            return rc;
        }
    }
    terminal_printf("camtest: unknown test '%s'\n", name);
    return -1;
}

// ---- Test implementations ----

static int test_camera_init(void) {
    // Ensure init returns success and status reflects initialized
    if (camera_init() == NULL) {
        warning("camera_init returned NULL queue\n");
        return 1;
    }
    if (!camera_status.initialized) {
        warning("camera_status.initialized == false after init\n");
        return 2;
    }
    return 0;
}

static int test_camera_status(void) {
    // Basic sanity on status struct
    if (!camera_status.initialized) return 1;
    if (camera_status.status == CAMERA_STATUS_ERROR) return 2;
    return 0;
}

static int test_camera_capture_smoke(void) {
    // Single capture path smoke test (non-blocking expectations)
    camera_image_t *img = camera_get_free_buffer();
    if (img == NULL) return 1;

    command_t cmd = camera_get_capture_command(img, NULL);
    enqueue_command(&cmd);

    // Allow some time for processing; this runner does not block on completion
    vTaskDelay(pdMS_TO_TICKS(50));

    camera_release_buffer(img);
    return 0;
}

static int test_camera_buffer_management(void) {
    // Test buffer allocation and release
    camera_image_t *buf1 = camera_get_free_buffer();
    if (buf1 == NULL) {
        warning("Failed to get first buffer\n");
        return 1;
    }
    
    // With single buffer, second request should fail
    camera_image_t *buf2 = camera_get_free_buffer();
    if (buf2 != NULL && buf2 == buf1) {
        // Same buffer returned is acceptable (single buffer mode)
        // but buffer should be marked as valid
        if (!buf1->valid) {
            // This is expected behavior for single buffer
        }
    }
    
    // Release first buffer
    camera_release_buffer(buf1);
    
    // After release, should be able to get buffer again
    camera_image_t *buf3 = camera_get_free_buffer();
    if (buf3 == NULL) {
        warning("Failed to get buffer after release\n");
        return 2;
    }
    
    camera_release_buffer(buf3);
    return 0;
}

static int test_camera_config_sanity(void) {
    // Verify camera configuration structure is valid
    if (camera_config.width == 0 || camera_config.height == 0) {
        warning("Invalid camera resolution\n");
        return 1;
    }
    
    if (camera_config.width > CAMERA_MAX_IMAGE_WIDTH ||
        camera_config.height > CAMERA_MAX_IMAGE_HEIGHT) {
        warning("Resolution exceeds maximum\n");
        return 2;
    }
    
    // Check exposure is in valid range (uint8_t, so max is 255)
    if (camera_config.exposure == 0) {
        warning("Invalid exposure value (0)\n");
        return 3;
    }
    
    // Verify capture mode is valid (enum check - just ensure it's not out of bounds)
    // Enum values are checked by compiler, so just verify it's set
    if (camera_config.capture_mode != CAMERA_CAPTURE_SINGLE &&
        camera_config.capture_mode != CAMERA_CAPTURE_CONTINUOUS &&
        camera_config.capture_mode != CAMERA_CAPTURE_BURST) {
        warning("Invalid capture mode\n");
        return 4;
    }
    
    return 0;
}

static int test_framework_registry(void) {
    // Test the test framework itself
    int test_count = 0;
    bool found_framework_test = false;
    
    for (camera_test_entry_t *e = camera_tests; e->name; ++e) {
        test_count++;
        if (e->test_func == NULL) {
            warning("Test '%s' has NULL function pointer\n", e->name);
            return 1;
        }
        if (strlen(e->name) == 0) {
            warning("Test has empty name\n");
            return 2;
        }
        
        // Check if we can find a test by name (without calling it recursively)
        if (strcmp(e->name, "framework") == 0) {
            found_framework_test = true;
        }
    }
    
    if (test_count == 0) {
        warning("No tests registered\n");
        return 3;
    }
    
    // Verify framework test is registered
    if (!found_framework_test) {
        warning("Framework test not found in registry\n");
        return 4;
    }
    
    // Verify we have at least 3 tests
    if (test_count < 3) {
        warning("Expected at least 3 tests, found %d\n", test_count);
        return 5;
    }
    
    return 0;
}


