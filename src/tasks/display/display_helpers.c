#include "display_task.h"

// Display Task memory structures
struct displayTaskMemory displayMem;
uint8_t displayQueueBuffer[COMMAND_QUEUE_MAX_COMMANDS * COMMAND_QUEUE_ITEM_SIZE];
QueueHandle_t displayQueue;

// Initialize the display task command queue
void display_task_init(void) {
    displayQueue = xQueueCreateStatic(COMMAND_QUEUE_MAX_COMMANDS, COMMAND_QUEUE_ITEM_SIZE, displayQueueBuffer, &displayMem.displayTaskQueue);

    if (displayQueue == NULL) {
        fatal("display: Failed to create display queue!\n");
    }
}
