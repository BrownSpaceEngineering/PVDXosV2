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

    // Initialize watchdog
    watchdog_init(WDT_CONFIG_PER_CYC16384, true);

    // Create tasks
    // xTaskCreateStatic(main_func, "Task Name", Stack Size, Parameters, Priority, Task Handle, Task Buffer, Task Control Block);

    // Create heartbeat task
    BaseType_t heartbeatCreateStatus = xTaskCreate(heartbeat_main, "Heartbeat", 1000, NULL, 1, NULL);
    if (heartbeatCreateStatus != pdPASS) {
        printf("Heartbeat Task Creation Failed!\n");
    } else {
        printf("Heartbeat Task Created!\n");
    }

    // Create watchdog task
    BaseType_t watchdogCreateStatus = xTaskCreate(watchdog_main, "Watchdog", 1000, NULL, 1, NULL);
    if (watchdogCreateStatus != pdPASS) {
        printf("Watchdog Task Creation Failed!\n");
    } else {
        printf("Watchdog Task Created!\n");
    }

    // Register tasks with the watchdog
    watchdog_register_task(WATCHDOG_TASK);
    watchdog_register_task(HEARTBEAT_TASK);

    // Start the scheduler
    vTaskStartScheduler();

    printf("Work completed -- Looping forever\n");
    while (true);
}
