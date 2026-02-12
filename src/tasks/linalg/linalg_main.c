/**
 * linalg_main.c
 *
 * Created: February 08, 2026
 * Authors:
 */

// Save the current diagnostic state
#pragma GCC diagnostic push 
// Turn off the specific warning
#pragma GCC diagnostic ignored "-Wunused-function" 
#pragma GCC diagnostic ignored "-Waddress"

#include "linalg_task.h"
#include "../../../lapack/EmbeddedLapack/src/LinearAlgebra/declareFunctions.h"

linalg_task_memory_t linalg_mem;

/*
 * Print a matrix A, with the dimension row x column
 */
void debug_matrix(double* A, int row, int column) {
	for(int i = 0; i < row; i++){
		for(int j = 0; j < column; j++){
			debug("%0.18f ", *(A++));
		}
		debug("\n");
	}
	debug("\n");

}


/**
 * \fn main_linalg
 *
 * \param pvParameters a void pointer to the parametres required by linalg; not currently set by config
 *
 * \warning should never return
 */
void main_linalg(void *pvParameters) {
    info("linalg: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time this task should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;

    double A[2*2] = {1, 2, 3, 4};
    double B[2*2] = {5, 6, 7, 8};
    double C[2*2]; 
    double C_elementwise[2*2] = {5, 12, 21, 32}; 
    double C_expected[2*2] = {19, 22, 43, 50};

    double det_a_true = -2;
    double det_a = det(A, 2);
    debug("det(A) = %f, should be %f\n", det_a, det_a_true);
    double det_b_true = -2;
    double det_b = det(B, 2);
    debug("det(B) = %f, should be %f\n", det_b, det_b_true);

    mul(A, B, false, C, 2, 2, 2);
    debug("C = A*B:\n");
    debug_matrix(C, 2, 2);
    debug("C should be:\n");
    debug_matrix(C_expected, 2, 2);

    mul(A, B, true, C, 2, 2, 2);
    debug("C = A.*B:\n");
    debug_matrix(C, 2, 2);
    debug("C should be:\n");
    debug_matrix(C_elementwise, 2, 2);

    while (true) {
        debug_impl("\n---------- linalg Task Loop ----------\n");

        // Block waiting for at least one command to appear in the command queue
        if (xQueueReceive(p_linalg_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            // Once there is at least one command in the queue, empty the entire queue
            do {
                debug("linalg: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_linalg(&cmd); 
                // TODO: implement contents of main loop. 

            } while (xQueueReceive(p_linalg_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("linalg: No more commands queued.\n");

        // Check in with the watchdog task
        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
        }
        debug("linalg: Enqueued watchdog checkin command\n");
    }
}


// Restore the previous diagnostic state (re-enables the warning)
#pragma GCC diagnostic pop 
