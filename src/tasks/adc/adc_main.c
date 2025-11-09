#include "adc_task.h"
#include "../shell/shell_helpers.h"

struct adcTaskMemory adcMem = {0};

void adc_main(void *pvParameters) {
    info("ADC task started\n");
    clear_RTT_input_buffer();
    terminal_printf(RTT_CTRL_TEXT_BRIGHT_YELLOW "\n\n\n\n\n\n\n\nPVDX ADC Initialized! [%s (%s:%s), Built %s]\n" RTT_CTRL_RESET,
                    BUILD_TYPE, GIT_BRANCH_NAME, GIT_COMMIT_HASH, BUILD_DATE " at " BUILD_TIME);

    terminal_printf("%s", ADC_ASCII_ART);
}