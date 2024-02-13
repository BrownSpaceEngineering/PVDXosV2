#include <atmel_start.h>
#include <driver_init.h>
#include <hal_adc_sync.h>

#include "SEGGER_RTT_printf.h"
#include "globals.h"
#include "heartbeat_task.h"
#include "rtos_start.h"

/*
Compilation guards to make sure that compilation is being done with the correct flags and correct compiler versions
If you want to get rid of the red squiggly lines:
- set C standard to GNU99 in the C/C++ extension settings
- Add "-DDEVBUILD" to the InteliSense settings as a compiler argument
*/

//Check GNU 99 standard
#if __STDC_VERSION__ != 199901L
    #error "This program needs to be compiled with the GNU99 Standard"
#endif

//Check that at least one of {DEVBUILD, UNITTEST, RELEASE} is defined
#if !defined(DEVBUILD) && !defined(UNITTEST) && !defined(RELEASE)
    #error "Build type flag not set! Must be one of: {DEVBUILD, UNITTEST, RELEASE}"
#endif
//Check that at most one of {DEVBUILD, UNITTEST, RELEASE} is defined
#if defined(DEVBUILD) && defined(UNITTEST)
    #error "Multiple build type flags set! Must be one of: {DEVBUILD, UNITTEST, RELEASE}"
#endif
#if defined(DEVBUILD) && defined(RELEASE)
    #error "Multiple build type flags set! Must be one of: {DEVBUILD, UNITTEST, RELEASE}"
#endif
#if defined(UNITTEST) && defined(RELEASE)
    #error "Multiple build type flags set! Must be one of: {DEVBUILD, UNITTEST, RELEASE}"
#endif


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
