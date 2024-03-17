#include "globals.h"

void runRM3100() {
    RM3100_return_t returnVal = values_loop();
    if (returnVal.x == 0) {
        return;
    }
}

int main(void)
{
            /* Initializes MCU, drivers and middleware */
            atmel_start_init();
            printf("ATMEL Initialization Complete!\n");

            init_rm3100();

            // Create Heartbeat Task
            //xTaskCreate(main_func, "Task Name", Stack Size, Parameters, Priority, Task Handle);
            // BaseType_t heartbeatCreateStatus = xTaskCreate(values_loop, "Heartbeat", 1000, NULL, 1, NULL);
            // if (heartbeatCreateStatus != pdPASS) {
            //     printf("Heartbeat Task Creation Failed!\n");
            // } else {
            //     printf("Heartbeat Task Created!\n");
            // }

            // Start the scheduler
            //vTaskStartScheduler();

            printf("Work completed -- Looping forever\n");
            while (1) {
                runRM3100();
            }
        }
