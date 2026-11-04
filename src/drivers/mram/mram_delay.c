#include "FreeRTOS.h"
#include "mram_driver.h"
#include "task.h"

// The SysTick-based delay_ms() must not be used once FreeRTOS owns SysTick
void mram_delay_ms(uint16_t ms) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        delay_ms(ms);
    } else {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}
