#include "reflash_task.h"
#include "FreeRTOS.h"

reflash_task_memory_t reflash_mem;

void reflash_bootloaders(bool sched_running) {
    (void)sched_running;
}

void main_reflash_task(void *pvParameters) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000 * 10)); // 1000 * 60 * 60 * 6
        reflash_bootloaders(true);
    }
}