#include "main.h"

// Buffer for Segger Logging Channel
uint8_t SEGGER_RTT_LOG_BUFFER[SEGGER_RTT_LOG_BUFFER_SIZE];

cosmicmonkeyTaskArguments_t cm_args = {0};

int main(void) {
    /* Initializes MCU, drivers and middleware */
    atmel_start_init();
    hardware_init();
    info_impl(RTT_CTRL_RESET RTT_CTRL_CLEAR); //Reset the terminal
    info_impl("--- Atmel & Hardware Initialization Complete ---\n");
    info_impl("[+] Build Type: %s\n", BUILD_TYPE);
    info_impl("[+] Build Date: %s\n", BUILD_DATE);
    info_impl("[+] Build Time: %s\n", BUILD_TIME);
    info_impl("[+] Built from branch: %s\n", GIT_BRANCH_NAME);
    info_impl("[+] Built from commit: %s\n", GIT_COMMIT_HASH);

    // Bootloader sets a magic number in backup RAM to indicate that it has run successfully
    uint32_t *p_magic_number = (uint32_t *)BOOTLOADER_MAGIC_NUMBER_ADDRESS;
    uint32_t magic_number = *p_magic_number;
    *p_magic_number = 0; // Clear the magic number so that this value doesn't linger
    if (magic_number == BOOTLOADER_MAGIC_NUMBER_VALUE) {
        info_impl("[+] Bootloader executed normally\n");
    } else {
        warning_impl("[!] Abnormal bootloader behavior (Magic Number: %x)\n", magic_number);
    }

    // Initialize the watchdog as early as possible to ensure that the system is reset if the initialization hangs
    watchdog_init(WDT_CONFIG_PER_CYC16384, true);
    info("Watchdog initialized\n");

    // xTaskCreateStatic(main_func, "TaskName", StackSize, pvParameters, Priority, StackBuffer, TaskTCB);

    // Create the TaskManager initialization task
    // The taskManager initializer initializes the watchdog and all necessary sensors
    TaskHandle_t taskManagerInitializerTaskHandle =
        xTaskCreateStatic(task_manager_init, "TaskManagerInit", TASK_MANAGER_TASK_STACK_SIZE, NULL, 2, taskManagerMem.taskManagerTaskStack,
                          &taskManagerMem.taskManagerTaskTCB);

    watchdog_register_task(TASK_MANAGER_TASK);

    if (taskManagerInitializerTaskHandle == NULL) {
        fatal("main: TaskManagerInitializer task creation failed!\n");
    } else {
        info("main: TaskManagerInitializer task created!\n");
    }

    // Create the display main task
    //  The display main task is a simple task that flips between two images in order to time our SPI transmission rates
    TaskHandle_t displayMainTaskHandle = xTaskCreateStatic(display_main, "DisplayMain", DISPLAYMAIN_TASK_STACK_SIZE, NULL, 1,
                                                           displayMainMem.displayMainTaskStack, &displayMainMem.displayMainTaskTCB);

    watchdog_register_task(DISPLAY_TASK);

    if (displayMainTaskHandle == NULL) {
        fatal("main: DisplayMain task creation failed!\n");
    } else {
        info("main: DisplayMain task created!\n");
    }

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

    // ------- SHELL TASK -------

    TaskHandle_t shellTaskHandle =
        xTaskCreateStatic(shell_main, "Shell", SHELL_TASK_STACK_SIZE, NULL, 2, shellMem.shellTaskStack, &shellMem.shellTaskTCB);

    watchdog_register_task(SHELL_TASK); // Register the shell task with the watchdog so that it can check in

    if (shellTaskHandle == NULL) {
        fatal("main: Shell task creation failed!\n");
    } else {
        info("main: Shell task created!\n");
    }

    // ------- COSMIC MONKEY TASK -------

    #if defined(UNITTEST) || defined(DEVBUILD)
        #if defined(UNITTEST)
        cm_args.frequency = 10;
        #endif
        #if defined(DEVBUILD)
        cm_args.frequency = 0;
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

void hardware_init() {
    // Segger Buffer 0 is pre-configured at compile time according to segger documentation
    // Config the logging output channel (assuming it's not zero)
    SEGGER_RTT_ConfigUpBuffer(LOGGING_RTT_OUTPUT_CHANNEL, "Log Output", SEGGER_RTT_LOG_BUFFER, SEGGER_RTT_LOG_BUFFER_SIZE,
                              SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    return;
}