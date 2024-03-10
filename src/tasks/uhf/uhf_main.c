#include "globals.h"
#include "uhf_hal.h"
#include "uhf_task.h"

struct uhfTaskMemory uhfMem = {0};

void uhf_main(void *pvParameters) {
    info("UHF task started!\n");
    
    // Initialize the UHF hardware
    status_t init_status = uhf_init(433E6);
    if (init_status == SUCCESS) {
        info("UHF module initialized succesfully\n");
    } else {
        warning("Failed to initialize UHF module! [Error: %d]\n", init_status);
    }

    while (1) {
        info("UHF task delaying forver\n");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
