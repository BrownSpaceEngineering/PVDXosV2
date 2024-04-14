#include <atmel_start.h>
#include <driver_init.h>
#include <hal_adc_sync.h>

#include "SEGGER_RTT_printf.h"
#include "rtos_start.h"
#include "heartbeat_task.h"

int main(void)
{
            /* Initializes MCU, drivers and middleware */
            atmel_start_init();
            printf("ATMEL Initialization Complete!\n");

            // Create Heartbeat Task
            // xTaskCreate(main_func, "Task Name", Stack Size, Parameters, Priority, Task Handle);
            BaseType_t heartbeatCreateStatus = xTaskCreate(heartbeat_main, "Heartbeat", 1000, NULL, 1, NULL);
            if (heartbeatCreateStatus != pdPASS) {
                printf("Heartbeat Task Creation Failed!\n");
            } else {
                printf("Heartbeat Task Created!\n");
            }

    // Start the scheduler
    vTaskStartScheduler();
    fatal("vTaskStartScheduler Returned! -- Should never happen!\n");
    while (true) {
        // Loop forever, but we should never get here anyways
    }
}

void hardware_init() {
    // Segger Buffer 0 is pre-configured at compile time according to segger documentation
    // Config the logging output channel (assuming it's not zero)
    if (LOGGING_RTT_OUTPUT_CHANNEL != 0) {
        SEGGER_RTT_ConfigUpBuffer(LOGGING_RTT_OUTPUT_CHANNEL, "Log Output", SEGGER_RTT_LOG_BUFFER, SEGGER_RTT_LOG_BUFFER_SIZE,
                                  SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    }
    return;
}