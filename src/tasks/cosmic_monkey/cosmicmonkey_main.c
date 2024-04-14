#include "cosmicmonkey_task.h"

void cosmicmonkey_main(void *pvParameters) {
    cosmicmonkeyTaskArguments_t args =
        *((cosmicmonkeyTaskArguments_t*) pvParameters);
    
    rand_sync_init(&RAND_0, TRNG); // Initialize random number generator
    rand_sync_enable(&RAND_0); // Enable the random number generator clock

    info("Cosmic monkey started with frequency: %d Hz\r\n", args.frequency);
    while (1)
    {
        if (perform_flip() != SUCCESS) {
            warning("Internal error occured\n");
        }
        int time_task = (1000 / args.frequency); // Generates the time delay in milliseconds based on the frequency
        vTaskDelay(pdMS_TO_TICKS(time_task));
    }
}