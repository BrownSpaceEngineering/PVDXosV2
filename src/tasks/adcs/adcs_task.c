/**
 * adcs_task.c
 *
 * RTOS task for ADCS functionality
 *
 * Created: September 20, 2025
 * Modified: November 24, 2025
 * Authors: Avinash Patel, Yi Lyo, Alexander Thaep
 */

#include "adcs_task.h"
#include "logging.h"
#include "device_checks.h"


/* ---------- DATA MANAGEMENT ------------------------ */

photodiode_data_t photodiode_data_buffer[2]; // Buffer to hold the last 2 photodiode readings
mag_data_t mag_data_buffer[2]; // Buffer to hold the last 2 magnetometer readings
SCH1_result_t gyro_data_buffer[2]; // Buffer to hold the last 2 gyro readings

status_t update_photodiode_data(photodiode_data_t *persistent_photo_data, size_t count ) {
    // first move previous photodiode data backwards in the array
    for (size_t i = count - 1; i > 0; i--) {
        persistent_photo_data[i] = persistent_photo_data[i - 1];
    }
    
    status_t result = photodiode_read(&persistent_photo_data[0]);
    
    if (result != SUCCESS) {
        warning("adcs task: photodiode read failed\n");
        // at this point we should check the device, as we've potentially run into a fault 
        // uncheck device to force check function to run
        uncheck_device(PHOTODIODE_ID);
        bool photo_status = check_device(PHOTODIODE_ID);

        if (!photo_status) {
            warning("adcs task: photodiode device check failed after read failure\n");
            // TODO: queue sth to task manage to update state variable. 
        }
    }

    return SUCCESS; 
}

status_t update_magnetometer_data(mag_data_t *persistent_readings, size_t n_reads) {
    mag_raw_reading_t raw_readings;
    
    // Push previous readings one step forwards
    for (size_t i = n_reads - 1; i > 0; i--) {
        persistent_readings[i] = persistent_readings[i - 1];
    }   

    status_t data_read = magnetometer_read(&raw_readings, &persistent_readings[0]);
    if (data_read != SUCCESS) {
        // First we uncheck the magnetometer
        uncheck_device(MAGNETOMETER_ID);
        bool mag_status = check_device(MAGNETOMETER_ID);
        
        if (!mag_status) {
            warning("adcs task: magnetometer device check failed after read failure\n");
            // TODO: Update status with magnetometer failure
        }
    }

    return SUCCESS;
}

status_t update_gyro_data(SCH1_result_t *persistent_gyro_data, size_t count ) {
    // first move previous gyro data backwards in the array
    for (size_t i = count - 1; i > 0; i--) {
        persistent_gyro_data[i] = persistent_gyro_data[i - 1];
    }
    
    status_t result = gyro_read(&persistent_gyro_data[0]);
    
    if (result != SUCCESS) {
        warning("adcs task: gyro read failed\n");
        // at this point we should check the device, as we've potentially run into a fault 
        uncheck_device(GYROSCOPE_ID);
        bool gyro_status = check_device(GYROSCOPE_ID);

        if (!gyro_status) {
            warning("adcs task: gyro device check failed after read failure\n");
            // TODO: queue sth to task manage to update state variable. 
        }
    }

    return SUCCESS;
}

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

/**
 * \fn get_adcs_process_command
 *
 * \brief Creates a command to do adcs stuff
 *
 * \param data pointer to data structure to fill
 *
 * \returns command_t command structure
 */
command_t get_adcs_process_command(photomagrtc_read_args_t *const args) {
    return (command_t) {
        .target = p_adcs_task,
        .operation = OPERATION_PROCESS,
        .p_data = &args,
        .len = sizeof(photomagrtc_read_args_t),
        .result = PROCESSING,
        .callback = NULL
    };
}

/**
 * \fn get_photomagrtc_read_command
 *
 * \brief Creates a command to read magnetometer, photodiode, rtc data
 *
 * \param data pointer to data structure to fill
 *
 * \returns command_t command structure
 */
command_t get_photomagrtc_read_command(
        mag_data_t *const mag_data, 
        photodiode_data_t *const photodiode_data,
        rtc_data_t *const rtc_data) 
    {
    photomagrtc_read_args_t args = {
        .mag_buffer = mag_data,
        .photodiode_buffer = photodiode_data,
        .rtc_buffer = rtc_data
    };

    return (command_t) {
        .target = p_adcs_task,
        .operation = OPERATION_READ,
        .p_data = &args,
        .len = sizeof(photomagrtc_read_args_t),
        .result = PROCESSING,
        .callback = NULL
    };
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn exec_command_adcs_process
 *
 * \brief Executes function corresponding to the command
 *
 * \param p_cmd a pointer to a command containing information for processing
 */
void exec_command_adcs_process(command_t *const p_cmd) {
    if (p_cmd->target != p_adcs_task) {
        fatal("adcs processing: command target is not adcs! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
    }

    rtc_data_t temp;

    photomagrtc_read_args_t *args = (photomagrtc_read_args_t *)p_cmd->p_data;
    status_t rtc_status = get_rtc_values(&temp);

    if (args == NULL) info("adcs: stuff happens here\n");

    // Do stuff with readings here

    info("ADCS microsecond count: %lu\n", temp.microseconds_count);
    info("ADCS seconds count: %lu\n", temp.seconds_count);
    info("ADCS mag reading [x,y,z]: [%f,%f,%f]\n", 
        mag_data_buffer[0].x, 
        mag_data_buffer[0].y,
        mag_data_buffer[0].z);

    if (rtc_status == SUCCESS) p_cmd->result = SUCCESS;
    p_cmd->result = ERROR_PROCESSING_FAILED;
}