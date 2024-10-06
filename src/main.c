#include "main.h"

// Buffer for Segger Logging Channel

CosmicMonkeyTaskArguments cm_args = {0};

static Status PVDX_init(void);

int main(void) {
    /* Initializes MCU, drivers and middleware */
    atmel_start_init();
    PVDX_init();
    info_impl(RTT_CTRL_RESET RTT_CTRL_CLEAR); // Reset the terminal
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

    // ------- INIT WATCHDOG, COMMAND_EXECUTOR, TASK_MANAGER TASKS (in that order) -------

    void (*main_functions[SUBTASK_START_INDEX])(void *pvParameters) = {
        watchdog_main,
        command_executor_main,
        task_manager_main,
    };

    void (*init_functions[SUBTASK_START_INDEX])(void) = {
        watchdog_init,
        command_executor_init,
        task_manager_init,
    };

    const char *task_names[SUBTASK_START_INDEX] = {"Watchdog Task", "Command Executor Task", "Task Manager Task"};

    for (size_t i = 0; i < SUBTASK_START_INDEX; i++) {
        init_functions[i]();

        if (task_list[i].function == main_functions[i]) {
            init_task(i);
        } else {
            fatal("%s not found at index %d of task list!\n", task_names[i], i);
        }

        info("%s initialized\n", task_names[i]);
    }

    // ------- COSMIC MONKEY TASK -------

#if defined(UNITTEST) || defined(DEVBUILD)
    #if defined(UNITTEST)
    cm_args.frequency = 10;
    #endif
    #if defined(DEVBUILD)
    cm_args.frequency = 1;
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

static Status PVDX_init() {
    // Segger Buffer 0 is pre-configured at compile time according to segger documentation
    // Config the logging output channel (assuming it's not zero)
    if (LOGGING_RTT_OUTPUT_CHANNEL != 0) {
        SEGGER_RTT_ConfigUpBuffer(LOGGING_RTT_OUTPUT_CHANNEL, "Log Output", SEGGER_RTT_LOG_BUFFER, SEGGER_RTT_LOG_BUFFER_SIZE,
                                  SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    }
    return SUCCESS;
}