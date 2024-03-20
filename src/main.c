#include "main.h"

cosmicmonkeyTaskArguments_t cm_args = {0};

int main(void) {
    /* Initializes MCU, drivers and middleware */
    atmel_start_init();
    info("--- ATMEL Initialization Complete ---\n");
    info("[+] Build Type: %s\n", BUILD_TYPE);
    info("[+] Build Date: %s\n", BUILD_DATE);
    info("[+] Build Time: %s\n", BUILD_TIME);
    info("[+] Built from branch: %s\n", GIT_BRANCH_NAME);
    info("[+] Built from commit: %s\n", GIT_COMMIT_HASH);

    // Initialize the watchdog as early as possible to ensure that the system is reset if the initialization hangs
    watchdog_init(WDT_CONFIG_PER_CYC16384, true);

    // xTaskCreateStatic(main_func, "TaskName", StackSize, pvParameters, Priority, StackBuffer, TaskTCB);

    // Create the heartbeat task
    // The heartbeat task is a simple task that blinks the LEDs in a pattern to indicate that the system is running
    TaskHandle_t heartbeatTaskHandle = xTaskCreateStatic(heartbeat_main, "Heartbeat", HEARTBEAT_TASK_STACK_SIZE, NULL, 1,
                                                         heartbeatMem.heartbeatTaskStack, &heartbeatMem.heartbeatTaskTCB);

    watchdog_register_task(HEARTBEAT_TASK); // Register the heartbeat task with the watchdog so that it can check in

    if (heartbeatTaskHandle == NULL) {
        fatal("main: Heartbeat task creation failed!\n");
    } else {
        info("main: Heartbeat task created!\n");
    }

    // Create watchdog task
    // The watchdog task is responsible for checking in with all the other tasks and resetting the system if a task has
    // not checked in within the allowed time
    TaskHandle_t watchdogTaskHandle = xTaskCreateStatic(watchdog_main, "Watchdog", WATCHDOG_TASK_STACK_SIZE, NULL, 2,
                                                        watchdogMem.watchdogTaskStack, &watchdogMem.watchdogTaskTCB);

    watchdog_register_task(WATCHDOG_TASK); // Register the watchdog task with itself so that it can check in

    if (watchdogTaskHandle == NULL) {
        fatal("main: Watchdog task creation failed!\n");
    } else {
        info("main: Watchdog task created!\n");
    }

#if defined(UNITTEST) || defined(DEVBUILD)
    #if defined(UNITTEST)
    cm_args.frequency = 10;
    #endif
    #if defined(DEVBUILD)
    cm_args.frequency = 5;
    #endif

    TaskHandle_t cosmicMonkeyTaskHandle =
        xTaskCreateStatic(cosmicmonkey_main, "CosmicMonkey", COSMICMONKEY_TASK_STACK_SIZE, (void *)&cm_args, 1,
                          cosmicmonkeyMem.cosmicmonkeyTaskStack, &cosmicmonkeyMem.cosmicmonkeyTaskTCB);
    if (cosmicMonkeyTaskHandle == NULL) {
        warning("Cosmic Monkey Task Creation Failed!\r\n");
    } else {
        info("Cosmic Monkey Task Created!\r\n");
    }
#endif // Cosmic Monkey

    // Start the scheduler
    vTaskStartScheduler();
    fatal("vTaskStartScheduler Returned! -- Should never happen!\n");
    while (true) {
        // Loop forever, but we should never get here anyways
    }
}
