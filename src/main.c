#include "SEGGER_RTT_printf.h"
#include "globals.h"
#include "heartbeat_task.h"
#include "rtos_start.h"
#include "watchdog_task.h"

#include <atmel_start.h>
#include <driver_init.h>
#include <hal_adc_sync.h>
#include <string.h>

/*
Compilation guards to make sure that compilation is being done with the correct flags and correct compiler versions
If you want to get rid of the red squiggly lines:
- set C standard to GNU99 in the C/C++ extension settings
- Add "-DDEVBUILD" to the IntelliSense settings as a compiler argument
*/

// Check GNU 99 standard
#if __STDC_VERSION__ != 199901L
    #error "This program needs to be compiled with the GNU99 Standard"
#endif

// Check that at least one of {DEVBUILD, UNITTEST, RELEASE} is defined
#if !defined(DEVBUILD) && !defined(UNITTEST) && !defined(RELEASE)
    #error "Build type flag not set! Must be one of: {DEVBUILD, UNITTEST, RELEASE}"
#endif
// Check that at most one of {DEVBUILD, UNITTEST, RELEASE} is defined
#if defined(DEVBUILD) && defined(UNITTEST)
    #error "Multiple build type flags set! (DEVBUILD && UNITTEST) Must be exactly one of: {DEVBUILD, UNITTEST, RELEASE}"
#endif
#if defined(DEVBUILD) && defined(RELEASE)
    #error "Multiple build type flags set! (DEVBUILD && RELEASE) Must be exactly one of: {DEVBUILD, UNITTEST, RELEASE}"
#endif
#if defined(UNITTEST) && defined(RELEASE)
    #error "Multiple build type flags set! (UNITTEST && RELEASE) Must be exactly one of: {DEVBUILD, UNITTEST, RELEASE}"
#endif

int main(void) {
    /* Initializes MCU, drivers and middleware */
    atmel_start_init();
    printf("--- ATMEL Initialization Complete ---\n");

    // Initialize the watchdog as early as possible to ensure that the system is reset if the initialization hangs
    watchdog_init(WDT_CONFIG_PER_CYC16384, true);

    // xTaskCreateStatic(main_func, "TaskName", StackSize, pvParameters, Priority, StackBuffer, TaskTCB);

    // Create the heartbeat task
    // The heartbeat task is a simple task that blinks the LEDs in a pattern to indicate that the system is running
    TaskHandle_t heartbeatTaskHandle = xTaskCreateStatic(heartbeat_main, "Heartbeat", HEARTBEAT_TASK_STACK_SIZE, NULL, 1,
                                                         heartbeatMem.heartbeatTaskStack, &heartbeatMem.heartbeatTaskTCB);

    watchdog_register_task(HEARTBEAT_TASK); // Register the heartbeat task with the watchdog so that it can check in

    if (heartbeatTaskHandle == NULL) {
        printf("main: Heartbeat task creation failed!\n");
    } else {
        printf("main: Heartbeat task created!\n");
    }

    // Create watchdog task
    // The watchdog task is responsible for checking in with all the other tasks and resetting the system if a task has
    // not checked in within the allowed time
    TaskHandle_t watchdogTaskHandle = xTaskCreateStatic(watchdog_main, "Watchdog", WATCHDOG_TASK_STACK_SIZE, NULL, 2,
                                                        watchdogMem.watchdogTaskStack, &watchdogMem.watchdogTaskTCB);

    watchdog_register_task(WATCHDOG_TASK); // Register the watchdog task with itself so that it can check in

    if (watchdogTaskHandle == NULL) {
        printf("main: Watchdog task creation failed!\n");
    } else {
        printf("main: Watchdog task created!\n");
    }

    // Start the scheduler
    vTaskStartScheduler();
    printf("vTaskStartScheduler Returned! -- Should never happen!\n");
    while (true) {
        // Loop forever, but we should never get here anyways
    }
}
