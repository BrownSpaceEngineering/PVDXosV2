#include "mram_task.h"

#include "mram_driver.h"
#include "mram_test.h"

extern mram_task_memory_t mram_mem;

void exec_command_mram(command_t *p_cmd) {
    switch (p_cmd->operation) {
        case OPERATION_MRAM:
            mram_request_t *request = (mram_request_t *)p_cmd->p_data;
            if (request->write) {
                p_cmd->result = mram_write(request->pos, request->len, request->buf);
            } else {
                p_cmd->result = mram_read(request->pos, request->len, request->buf);
            }
            break;

        default:
            fatal("mram: Invalid operation! target: %d operation: %d\n", p_cmd->target, p_cmd->operation);
            break;
    }
}

void main_mram(void *pvParameters) {
    info("mram: Task Started!\n");

    // Obtain a pointer to the current task within the global task list
    pvdx_task_t *const current_task = get_current_task();
    // Cache the watchdog checkin command to avoid creating it every iteration
    command_t cmd_checkin = get_watchdog_checkin_command(current_task);
    // Calculate the maximum time the command dispatcher should block (and thus be unable to check in with the watchdog)
    const TickType_t queue_block_time_ticks = get_command_queue_block_time_ticks(current_task);
    // Varible to hold commands popped off the queue
    command_t cmd;

    while (true) {
        debug_impl("\n---------- MRAM Task Loop ----------\n");

        // Execute all commands contained in the queue
        if (xQueueReceive(p_mram_task->command_queue, &cmd, queue_block_time_ticks) == pdPASS) {
            do {
                debug("mram: Command popped off queue. Target: %d, Operation: %d\n", cmd.target, cmd.operation);
                exec_command_mram(&cmd);
            } while (xQueueReceive(p_mram_task->command_queue, &cmd, 0) == pdPASS);
        }
        debug("mram: No more commands queued.\n");

        if (should_checkin(current_task)) {
            enqueue_command(&cmd_checkin);
            debug("mram: Enqueued watchdog checkin command\n");
        }
    }
}

QueueHandle_t init_mram(void) {
    fatal_on_error(mram_init_hardware(), "mram: Hardware initialization failed!");
    mram_test();

    // Initialize the MRAM command queue
    QueueHandle_t mram_command_queue_handle =
        xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, mram_mem.mram_command_queue_buffer,
                           &mram_mem.mram_task_queue);
    if (mram_command_queue_handle == NULL) {
        fatal("Failed to create MRAM queue!\n");
    }

    return mram_command_queue_handle;
}