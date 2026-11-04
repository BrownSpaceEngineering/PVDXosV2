#include "cmd_dispatcher.h"
#include "globals.h"
#include "queue.h"
#include "stdint.h"

#define CMD_LOG_SZ 16 // The number of commands to keep in the log buffer for debugging purposes


static cmd_record_t cmd_log_buffer[CMD_LOG_SZ]; 
static uint8_t log_index = 0; // Index to keep track of where to log the next command in the buffer

/**
 * \fn log_command
 * 
 * \brief Log a command in the command log buffer for telemetry purposes
 * \param[in] p_cmd a pointer to the command struct to be logged
 */
void log_command(const command_t *p_cmd) {

    cmd_log_buffer[log_index].target = p_cmd->target;
    cmd_log_buffer[log_index].data_type = p_cmd->data_type;
    cmd_log_buffer[log_index].operation = p_cmd->operation;

    log_index = (log_index + 1) % CMD_LOG_SZ; // Increment index and wrap around if it exceeds buffer size
}


/**
 * \fn get_n_logs
 *
 * \brief Get the last n logged commands from the command log buffer
 *
 * \param[in] n the number of recent commands to retrieve
 * \param[out] logs an array to store the retrieved command logs, must be at least size n
 * 
 * \return status_t, whether retrieval was sucessful
 */
status_t get_n_logs(uint8_t n, cmd_record_t *logs) {

    if (n > CMD_LOG_SZ) {
        return ERROR_BAD_ARGS; // Cannot retrieve more logs than the buffer size
    }

    for (uint8_t i = 0; i < n; i++) {
        logs[i] = cmd_log_buffer[log_index];
        log_index = (log_index == 0) ? (CMD_LOG_SZ - 1) : (log_index - 1); // Move to the previous log, wrap around if necessary
    }

    return SUCCESS;
}


/**
 * \fn dispatch_command
 * 
 * \brief Forward command to the appropriate task for execution and 
 * log the command for telemetry
 * 
 * \param[in] p_cmd a pointer to the command struct to be dispatched
 * 
 * \return status_t, whether the forwarding was successful or not
 */
status_t enqueue_command(command_t *p_cmd) {
   
    pvdx_task_t *target = p_cmd->target;
    
    // Log cmd into buffer for telemetry
    log_command(p_cmd); 

    // Check if target is valid and enabled
    if (target == NULL) {
        return ERROR_BAD_TARGET;
    }
    // The enabled check and zero-wait enqueue must be atomic with task-manager
    // transitions. Otherwise a producer paused across disable/resume can enqueue
    // a stale capture after the camera's shutdown queue reset.
    taskENTER_CRITICAL();
    status_t result;
    if (!target->enabled) {
        result = ERROR_TASK_DISABLED;
    } else if (xQueueSendToBack(target->command_queue, p_cmd, 0) != pdTRUE) {
        result = ERROR_QUEUE_ERROR;
    } else {
        result = SUCCESS;
    }
    taskEXIT_CRITICAL();
    return result;
} 

