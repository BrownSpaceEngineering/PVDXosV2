#include "mram_driver.h"

// Stage 2 runs without an RTOS, so the SysTick-based busy wait is fine
void mram_delay_ms(uint16_t ms) {
    delay_ms(ms);
}
