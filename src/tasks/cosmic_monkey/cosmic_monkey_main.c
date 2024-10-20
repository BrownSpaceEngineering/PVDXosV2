#include "cosmic_monkey_task.h"

cosmic_monkey_task_memory_t cosmic_monkey_mem;

void main_cosmic_monkey(void *pvParameters) {
    cosmic_monkey_task_arguments_t args = *((cosmic_monkey_task_arguments_t *)pvParameters);

    rand_sync_init(&RAND_0, TRNG); // Initialize random number generator
    rand_sync_enable(&RAND_0);     // Enable the random number generator clock

    if (args.frequency == 0) {
        warning("Cosmic Monkey task started, but frequency is 0Hz! Suspending task forever...\n");
        vTaskSuspend(NULL);
    }

    info("Cosmic monkey started with frequency: %d Hz\n", args.frequency);
    while (1) {
        if (perform_flip() != SUCCESS) {
            warning("Internal error occured\n");
        }
        int time_task = (1000 / args.frequency); // Generates the time delay in milliseconds based on the frequency
        vTaskDelay(pdMS_TO_TICKS(time_task));
    }
}