#include <atmel_start.h>
#include <driver_init.h>
#include <hal_adc_sync.h>
#include <string.h>

#include "SEGGER_RTT_printf.h"
#include "globals.h"
#include "heartbeat_task.h"
#include "rtos_start.h"
#include "cosmicmonkey_task.h"

/*
Compilation guards to make sure that compilation is being done with the correct flags and correct compiler versions
If you want to get rid of the red squiggly lines:
- set C standard to GNU99 in the C/C++ extension settings
- Add "-DDEVBUILD" to the IntelliSense settings as a compiler argument
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
    #error "Multiple build type flags set! (DEVBUILD && UNITTEST) Must be exactly one of: {DEVBUILD, UNITTEST, RELEASE}"
#endif
#if defined(DEVBUILD) && defined(RELEASE)
    #error "Multiple build type flags set! (DEVBUILD && RELEASE) Must be exactly one of: {DEVBUILD, UNITTEST, RELEASE}"
#endif
#if defined(UNITTEST) && defined(RELEASE)
    #error "Multiple build type flags set! (UNITTEST && RELEASE) Must be exactly one of: {DEVBUILD, UNITTEST, RELEASE}"
#endif

struct cosmicmonkeyTaskArguments cm_args = {0};

int main(void)
{
            /* Initializes MCU, drivers and middleware */
            atmel_start_init();
            printf("--- ATMEL Initialization Complete ---\r\n");

            //Create the heartbeat task
            //The heartbeat task is a simple task that blinks the LEDs in a pattern to indicate that the system is running
            //xTaskCreateStatic(main_func, "TaskName", StackSize, pvParameters, Priority, StackBuffer, TaskTCB);
            TaskHandle_t heartbeatTaskHandle =
                xTaskCreateStatic(heartbeat_main, "Heartbeat", HEARTBEAT_TASK_STACK_SIZE, NULL, 1,
                                  heartbeatMem.heartbeatTaskStack, &heartbeatMem.heartbeatTaskTCB);
            if (heartbeatTaskHandle == NULL) {
                printf("Heartbeat Task Creation Failed!\r\n");
            } else {
                printf("Heartbeat Task Created!\r\n");
            }
            #if defined(UNITTEST) || defined(DEVBUILD)
            #if defined(UNITTEST)
                cm_args.frequency = 10;
            #endif
            #if defined(DEVBUILD)
                cm_args.frequency = 5;
            #endif

            TaskHandle_t cosmicMonkeyTaskHandle =
                xTaskCreateStatic(cosmicmonkey_main, "CosmicMonkey", COSMICMONKEY_TASK_STACK_SIZE, (void *) &cm_args, 1,
                                    cosmicmonkeyMem.cosmicmonkeyTaskStack, &cosmicmonkeyMem.cosmicmonkeyTaskTCB);
            if (cosmicMonkeyTaskHandle == NULL) {
                printf("Cosmic Monkey Task Creation Failed!\r\n");
            } else {
                printf("Cosmic Monkey Task Created!\r\n");
            }
            #endif // Cosmic Monkey
            // Starts the scheduler: this function never returns, since control is transferred to the RTOS scheduler and tasks begin to run.
            vTaskStartScheduler();
            printf("vTaskStartScheduler Returned: WE SHOULD NEVER GET HERE!\r\n");
            while (1) {
                //Should never get here anyways
            }
        }
