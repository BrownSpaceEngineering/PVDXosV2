#include "display_task.h"

// Initialize the display task command queue
void display_task_init(void) {
    displayQueue =
        xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, displayQueueBuffer, &display_mem.displayTaskQueue);

    if (displayQueue == NULL) {
        fatal("display: Failed to create display queue!\n");
    }
}
