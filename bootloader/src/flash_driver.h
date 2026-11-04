#ifndef FLASH_DRIVER_H
#define FLASH_DRIVER_H

// NVM flash helpers for stage 2 of the bootloader, on top of the ASF flash HAL (FLASH_0)

#include <stdbool.h>
#include <stdint.h>

#include "atmel_start.h"

bool program_flash_block(uint32_t flash_addr, uint8_t *data, uint32_t size);
void disable_flash_caches(void);
void enable_flash_caches(void);

#endif // FLASH_DRIVER_H
