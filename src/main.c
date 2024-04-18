#include "main.h"

cosmicmonkeyTaskArguments_t cm_args = {0};

int main(void) {
    /* Initializes MCU, drivers and middleware */
    atmel_start_init();
    info_impl("--- ATMEL Initialization Complete ---\n");
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

    // Initialize the task manager, which will initialize all other tasks on the system
    task_manager_init();
    info("Task Manager initialized\n");


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
