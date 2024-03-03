#include "uhf_task.h"

struct uhfTaskMemory uhfMem = {0};

void uhf_main(void *pvParameters) {
    info("UHF task started!\n");
    struct io_descriptor *io __attribute__((unused));
    uint16_t data __attribute__((unused));
    spi_m_os_get_io_descriptor(&SPI_0, &io);
    /* Control the slave select(SS) pin */
    gpio_set_pin_level(UHF_CS, false);
}
