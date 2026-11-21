#include "mram_driver.h"
#include "os_delay.h"

// The bootloader, which shares the MRAM driver, has its own mram_delay_ms()
void mram_delay_ms(uint16_t ms) {
    os_delay_ms(ms);
}
