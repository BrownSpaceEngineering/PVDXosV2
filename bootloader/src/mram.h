#ifndef MRAM_H
#define MRAM_H

// Set this flag to commit the OS and bootloaders from flash into MRAM (for development)
// #define MRAM_OS_WRITE

// Set this flag to load the OS from MRAM on boot and reflash the bootloaders from MRAM
// #define MRAM_OS_READ

#define MRAM_OS_BASE_ADDRESS    (0x00000000)
#define MRAM_OS_SIZE            (0x20000)

#define MRAM_FLASH_BASE_ADDRESS (0x00020000)
#define MRAM_FLASH_SIZE         (0x20000)

#include "atmel_start.h"
#include "hal_gpio.h"
#include "hal_delay.h"

void mram_init(void);
void mram_read_bytes(uint32_t address, uint8_t *data, uint32_t size);
void mram_write_bytes(uint32_t address, const uint8_t *data, uint32_t size);

#endif