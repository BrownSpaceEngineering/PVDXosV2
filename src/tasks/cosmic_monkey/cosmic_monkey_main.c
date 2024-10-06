#include "cosmicmonkey_task.h"

void cosmicmonkey_main(void *pvParameters) {
    CosmicMonkeyTaskArguments args = *((CosmicMonkeyTaskArguments *)pvParameters);

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