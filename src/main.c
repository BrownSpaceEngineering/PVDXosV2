#include <atmel_start.h>
#include <driver_init.h>
#include <hal_adc_sync.h>

#include "SEGGER_RTT_printf.h"
#include "rtos_start.h"
#include "heartbeat_task.h"
#include "watchdog_task.h"

int main(void)
{
    /* Initializes MCU, drivers and middleware */
    atmel_start_init();
    printf("ATMEL Initialization Complete!\n");

    // Initialize Watchdog
    watchdog_init(WDT_CONFIG_PER_CYC16384, true);

    // Create Tasks
    // xTaskCreate(main_func, "Task Name", Stack Size, Parameters, Priority, Task Handle);

    // Create Heartbeat Task
    BaseType_t heartbeatCreateStatus = xTaskCreate(heartbeat_main, "Heartbeat", 1000, NULL, 1, NULL);
    if (heartbeatCreateStatus != pdPASS) {
        printf("Heartbeat Task Creation Failed!\n");
    } else {
        printf("Heartbeat Task Created!\n");
    }

    // Create Watchdog Task
    BaseType_t watchdogCreateStatus = xTaskCreate(watchdog_main, "Watchdog", 1000, NULL, 1, NULL);

    // Start the scheduler
    vTaskStartScheduler();

    printf("Work completed -- Looping forever\n");
    while (true);
}
