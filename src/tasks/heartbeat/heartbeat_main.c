#include "heartbeat_task.h"
#include "SEGGER_RTT_printf.h"

void heartbeat_main(void *pvParameters) {
    printf("heartbeat: Task started!\n");

    while(1) {
        // Print the current time
        uint32_t current_time = xTaskGetTickCount();
        printf("heartbeat: Current time is %d\n", current_time);

        // Heartbeat pattern
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

        watchdog_checkin(HEARTBEAT_TASK); // Check in with the watchdog
    }
}