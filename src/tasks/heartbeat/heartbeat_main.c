#include "heartbeat_task.h"
#include "SEGGER_RTT_printf.h"

void heartbeat_main(void *pvParameters) {
    printf("Heartbeat Task Started!\n");
    while(1) {
        gpio_set_pin_level(LED_Orange1, false);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_pin_level(LED_Orange2, false);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_pin_level(LED_Red, true);
        vTaskDelay(pdMS_TO_TICKS(400));
        gpio_set_pin_level(LED_Orange1, true);
        vTaskDelay(pdMS_TO_TICKS(33));
        gpio_set_pin_level(LED_Orange2, true);
        vTaskDelay(pdMS_TO_TICKS(33));
        gpio_set_pin_level(LED_Red, false);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}