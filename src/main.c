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
    printf("main: ATMEL initialization complete!\n");

    // Initialize watchdog
    watchdog_init(WDT_CONFIG_PER_CYC16384, true);

    // Create tasks

    // Create heartbeat task
    BaseType_t heartbeatCreateStatus = xTaskCreate(heartbeat_main, "Heartbeat", 1000, NULL, 1, NULL);
    if (heartbeatCreateStatus != pdPASS) {
        printf("main: Heartbeat task creation failed!\n");
    } else {
        printf("main: Heartbeat task created!\n");
    }

    // Create watchdog task
    BaseType_t watchdogCreateStatus = xTaskCreate(watchdog_main, "Watchdog", 1000, NULL, 1, NULL);
    if (watchdogCreateStatus != pdPASS) {
        printf("main: Watchdog task creation failed!\n");
    } else {
        printf("main: Watchdog task created!\n");
    }

    // Register tasks with the watchdog
    watchdog_register_task(WATCHDOG_TASK);
    watchdog_register_task(HEARTBEAT_TASK);

    // Start the scheduler
    vTaskStartScheduler();

    printf("main: Work completed -- looping forever\n");
    while (true);
}
