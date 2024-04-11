#include "shell_task.h"

struct shellTaskMemory shellMem = {0};

void shell_main(void *pvParameters) {
    info("Shell task started\n");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}