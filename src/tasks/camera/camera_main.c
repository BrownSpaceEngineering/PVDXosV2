/**
 * camera_main.c
 *
 * Main loop of the ArduCam camera task which handles image capture commands,
 * auto-exposure algorithms, and camera configuration.
 *
 * Created: January 24, 2025
 * Authors: PVDX Team
 */

#include "camera.h"
#include "FreeRTOS.h"
#include "task.h"

// Note: camera_mem is defined in camera.c

/**
 * \fn main_camera
 *
 * \param pvParameters a void pointer to the parameters required by the camera task
 *
 * \warning should never return
 */
void camera_main(void *pvParameters) {
    info("camera: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time this task should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Variable to hold commands popped off the queue
    command_t cmd;
    
    // Continuous capture variables
    uint32_t last_capture_time = 0;
    bool continuous_capture_active = false;

    info("camera: Initialized with resolution %dx%d, format %d, auto-exposure %s\n", 
         camera_config.width, camera_config.height, camera_config.format,
         camera_config.auto_exposure_enabled ? "enabled" : "disabled");

    while (true) {
        debug_impl("\n---------- Camera Task Loop ----------\n");

        // Handle continuous capture mode
        if (camera_config.capture_mode == CAMERA_CAPTURE_CONTINUOUS && camera_status.initialized) {
            uint32_t current_time = xTaskGetTickCount() / portTICK_PERIOD_MS;
            
            if (!continuous_capture_active) {
                continuous_capture_active = true;
                last_capture_time = current_time;
                info("camera: Starting continuous capture mode (interval: %d ms)\n", 
                     camera_config.capture_interval_ms);
            }
            
            // Check if it's time for the next capture (convert ms to seconds)
            if ((current_time - last_capture_time) >= (camera_config.capture_interval_ms / 1000)) {
                camera_image_t *free_buffer = camera_get_free_buffer();
                if (free_buffer) {
                    command_t capture_cmd = camera_get_capture_command(free_buffer, NULL);
                    enqueue_command(&capture_cmd);
                    last_capture_time = current_time;
                    debug("camera: Enqueued continuous capture command\n");
                } else {
                    warning("camera: No free buffers available for continuous capture\n");
                }
            }
        } else if (continuous_capture_active) {
            continuous_capture_active = false;
            info("camera: Stopped continuous capture mode\n");
        }

        // Block waiting for at least one command to appear in the command queue
        if (xQueueReceive(p_camera_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            // Once there is at least one command in the queue, empty the entire queue
            do {
                debug("camera: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                camera_exec_command(&cmd);
                
                // Handle configuration changes that might affect continuous capture
                if (cmd.operation == OPERATION_CAMERA_CONFIG) {
                    camera_config_args_t *args = (camera_config_args_t*)cmd.p_data;
                    if (args && args->config) {
                        if (args->config->capture_mode != CAMERA_CAPTURE_CONTINUOUS && continuous_capture_active) {
                            continuous_capture_active = false;
                            info("camera: Disabled continuous capture due to configuration change\n");
                        }
                    }
                }
                
            } while (xQueueReceive(p_camera_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("camera: No more commands queued.\n");

        // Perform periodic auto-exposure calibration if enabled
        if (camera_config.auto_exposure_enabled && camera_status.initialized && 
            camera_config.capture_mode == CAMERA_CAPTURE_CONTINUOUS) {
            
            // Perform auto-exposure calibration every 10 captures
            if (camera_status.images_captured % 10 == 0 && camera_status.images_captured > 0) {
                command_t ae_cmd = camera_get_auto_exposure_command(CAMERA_AE_TARGET_BRIGHTNESS, false);
                enqueue_command(&ae_cmd);
                debug("camera: Enqueued periodic auto-exposure calibration\n");
            }
        }

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("camera: Enqueued watchdog checkin command\n");
        }
        
        // Small delay to prevent excessive CPU usage
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * \fn exec_command_camera
 *
 * \brief Executes camera commands received through the command queue
 *
 * \param p_cmd pointer to command to execute
 */
void camera_exec_command(command_t *const p_cmd) {
    if (!p_cmd) {
        fatal("camera: Received NULL command pointer\n");
    }

    switch (p_cmd->operation) {
        case OPERATION_CAMERA_CAPTURE: {
            camera_capture_args_t *args = (camera_capture_args_t*)p_cmd->p_data;
            if (args && args->image_buffer) {
                p_cmd->result = camera_capture_image(args->image_buffer, args->capture_config);
                debug("camera: Capture command executed with result %d\n", p_cmd->result);
            } else {
                p_cmd->result = ERROR_SANITY_CHECK_FAILED;
                warning("camera: Invalid capture command arguments\n");
            }
            break;
        }
        
        case OPERATION_CAMERA_CONFIG: {
            camera_config_args_t *args = (camera_config_args_t*)p_cmd->p_data;
            if (args && args->config) {
                p_cmd->result = camera_configure(args->config);
                debug("camera: Config command executed with result %d\n", p_cmd->result);
            } else {
                p_cmd->result = ERROR_SANITY_CHECK_FAILED;
                warning("camera: Invalid config command arguments\n");
            }
            break;
        }
        
        case OPERATION_CAMERA_STATUS: {
            camera_status_args_t *args = (camera_status_args_t*)p_cmd->p_data;
            if (args && args->status) {
                p_cmd->result = camera_get_status(args->status);
                debug("camera: Status command executed with result %d\n", p_cmd->result);
            } else {
                p_cmd->result = ERROR_SANITY_CHECK_FAILED;
                warning("camera: Invalid status command arguments\n");
            }
            break;
        }
        
        case OPERATION_CAMERA_AUTO_EXPOSURE: {
            camera_auto_exposure_args_t *args = (camera_auto_exposure_args_t*)p_cmd->p_data;
            if (args) {
                p_cmd->result = camera_auto_exposure_calibrate(args->target_brightness, args->force_recalibration);
                debug("camera: Auto-exposure command executed with result %d\n", p_cmd->result);
            } else {
                p_cmd->result = ERROR_SANITY_CHECK_FAILED;
                warning("camera: Invalid auto-exposure command arguments\n");
            }
            break;
        }
        
        default:
            warning("camera: Unknown command operation %d\n", p_cmd->operation);
            p_cmd->result = ERROR_BAD_TARGET;
            break;
    }
    
    // Free command arguments memory if allocated
    if (p_cmd->p_data) {
        free((void*)p_cmd->p_data);  // Cast away const for free
        // Note: Cannot set p_data to NULL because it's const
    }
}
