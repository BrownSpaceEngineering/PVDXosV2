/**
 * Code for the RM3100 Magnetometer Sensor
 *
 * Created: Dec 7, 2023 2:22 AM
 * Authors: Nathan Kim, Alexander Thaep, Siddharta Laloux, Tanish Makadia, Defne Doken, Aidan Wang
 */

#include "magnetometer_task.h"

extern magnetometer_task_memory_t magnetometer_mem;

/* ---------- DISPATCHABLE FUNCTIONS (sent as commands through the command dispatcher task) ---------- */

status_t magnetometer_store(void) {
    // TODO: Once the DataStore is fully implemented, store the reading there.
}

status_t magnetometer_read(int32_t *const raw_readings, float *const gain_adj_readings) {
    debug("magnetometer: Reading x,y,z data");
    return mag_read_data(raw_readings, gain_adj_readings);
}

/* ---------- NON-DISPATCHABLE FUNCTIONS (do not go through the command dispatcher) ---------- */

/**
 * \fn exec_command_magnetometer
 * 
 * \brief Executes a command received by the magnetometer task
 * 
 * \param p_cmd Pointer to the received command
 */
void exec_command_magnetometer(command_t *const p_cmd) {
    if (p_cmd->target != p_magnetometer_task) {
        fatal("magnetometer: command target is not magnetometer! target: %d operation: %d\n", p_cmd->target->name, p_cmd->operation);
    }

    switch (p_cmd->operation) {
        case OPERATION_READ:
            magnetometer_read_args_t args = *p_cmd->p_data;
            p_cmd->result = magnetometer_read(args.raw_readings, args.gain_adj_readings);
            break;
        default:
            fatal("magnetometer: Invalid operation! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
            break;
    }
}

/**
 * \fn init_magnetometer
 * 
 * \brief Initializes the magnetometer task, including hardware setup and command queue creation
 */
QueueHandle_t init_magnetometer(void) {
    init_rm3100();

    // Initialize the magnetometer command queue
    QueueHandle_t magnetometer_command_queue_handle =
        xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, magnetometer_mem.magnetometer_command_queue_buffer,
                           &magnetometer_mem.magnetometer_task_queue);
    if (magnetometer_command_queue_handle == NULL) {
        fatal("Failed to create magnetometer queue!\n");
    }

    return magnetometer_command_queue_handle;
}
