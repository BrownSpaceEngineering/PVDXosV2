#include "globals.h"
#include "uhf_hal.h"
#include "uhf_task.h"

struct uhfTaskMemory uhfMem = {0};
unsigned char uhf_test_message[] = "Brown UHF Engineering! PVDX PVDX PVDX PVDX";
size_t uhf_test_message_length = sizeof(uhf_test_message);

void uhf_main(void *pvParameters) {
    info("UHF task started!\n");
    
    // Initialize the UHF hardware
    status_t init_status = uhf_init(433E6);
    if (init_status == SUCCESS) {
        info("UHF module initialized succesfully\n");
    } else {
        warning("Failed to initialize UHF module! [Error: %d]\n", init_status);
    }

    status_t send_status = uhf_send(uhf_test_message, uhf_test_message_length);
    if (send_status == SUCCESS) {
        info("UHF message sent succesfully\n");
    } else {
        warning("Failed to send UHF message! [Error: %d]\n", send_status);
    }

    while (1) {
        info("UHF task done, delaying forver\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        uhf_send(uhf_test_message, uhf_test_message_length);
    }
}
