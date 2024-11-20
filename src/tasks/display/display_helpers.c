#include "display_task.h"

// Initialize the display task command queue
void init_display_task(void) {
    display_command_queue_handle =
        xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, display_command_queue_buffer, &display_mem.display_task_queue);

    if (display_command_queue_handle == NULL) {
        fatal("display: Failed to create display queue!\n");
    }
}
