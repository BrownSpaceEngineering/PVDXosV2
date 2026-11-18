#ifndef FLASH_DRIVER_H
#define FLASH_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#include "image_format.h"

#define FLASH_REPAIR_BLOCK_SIZE IMAGE_BLOCK_SIZE
#define FLASH_REPAIR_BLOCK_COUNT FLASH_OS_BLOCK_COUNT
#define FLASH_REPAIR_PAGE_SIZE (512u)

void flash_disable_caches(void);
void flash_enable_caches(void);
bool flash_program_block(uint32_t address, const uint8_t *data);

/**
 * \brief Reads the `ECCERR` register on flash, which automatically clears it.
 * \returns Refer to 25.8.11 in the SAMD51 data sheet.
 * \warning Reading the `ECCERR` register clears it.
 */
uint32_t get_eccerr_status(void);

#endif
