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

            printf("Work completed -- Looping forever\n");
            while (1) {
                
            }
        }
