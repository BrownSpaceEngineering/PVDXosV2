/**
 * photodiode_main.c
 *
 * Main loop of the Photodiode task which handles sun sensing for ADCS.
 * Supports 13-21 photodiodes with configurable sampling rates (0.1-100 Hz).
 *
 * Created: September 20, 2024
 * Authors: [Your Name]
 */

#include "photodiode_task.h"

// Photodiode Task memory structures
photodiode_task_memory_t photodiode_mem;

/**
 * \fn main_photodiode
 *
 * \param pvParameters a void pointer to the parametres required by photodiode; not currently set by config
 *
 * \warning should never return
 */
void main_photodiode(void *pvParameters) {
    info("photodiode: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time this task should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Variable to hold commands popped off the queue
    command_t cmd;
    
    // Calculate sampling delay based on configured rate
    TickType_t sampling_delay_ticks = pdMS_TO_TICKS(1000.0f / photodiode_config.sample_rate_hz);
    
    // Data buffer for photodiode readings
    photodiode_data_t photodiode_data;
    
    info("photodiode: Initialized with %d photodiodes at %.2f Hz\n", 
         photodiode_config.photodiode_count, photodiode_config.sample_rate_hz);

    while (true) {
        debug_impl("\n---------- Photodiode Task Loop ----------\n");

        // Block waiting for at least one command to appear in the command queue
        if (xQueueReceive(p_photodiode_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            // Once there is at least one command in the queue, empty the entire queue
            do {
                debug("photodiode: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_photodiode(&cmd);
                
                // Handle configuration changes
                if (cmd.operation == OPERATION_PHOTODIODE_CONFIG) {
                    // Recalculate sampling delay if configuration changed
                    sampling_delay_ticks = pdMS_TO_TICKS(1000.0f / photodiode_config.sample_rate_hz);
                    debug("photodiode: Updated sampling delay to %d ticks (%.2f Hz)\n", 
                          sampling_delay_ticks, photodiode_config.sample_rate_hz);
                }
                
            } while (xQueueReceive(p_photodiode_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("photodiode: No more commands queued.\n");

        // Perform periodic photodiode reading based on configured sampling rate
        // This ensures continuous sun sensing data for the ADCS system
        command_t read_cmd = get_photodiode_read_command(&photodiode_data);
        enqueue_command(&read_cmd);
        
        // Wait for the command to be processed
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to allow command processing
        
        if (read_cmd.result == SUCCESS && photodiode_data.valid) {
            debug("photodiode: Sun vector [%.3f, %.3f, %.3f] from %d photodiodes\n",
                  photodiode_data.sun_vector[0], photodiode_data.sun_vector[1], 
                  photodiode_data.sun_vector[2], photodiode_data.active_count);
        } else {
            warning("photodiode: Failed to read photodiode data\n");
        }

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("photodiode: Enqueued watchdog checkin command\n");
        }
        
        // Wait for next sampling period
        vTaskDelay(sampling_delay_ticks);
    }
}
