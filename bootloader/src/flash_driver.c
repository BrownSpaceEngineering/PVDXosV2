/**
 * flash_driver.c
 *
 * NVM flash helpers for stage 2 of the bootloader: rewriting one erase block with read-back, and turning the flash
 * caches off while flash is being written and back on before PVDXos starts.
 *
 * Created: November 3, 2026
 */

#include "flash_driver.h"

#include <string.h>

#include "hal_cache.h"
#include "hri_nvmctrl_d51.h"

#define PROGRAM_ATTEMPTS 3
#define FLASH_LOCK_REGIONS 32 // NVMCTRL lock regions (NVMCTRL_REGIONS_NUM in hpl_nvmctrl.c)

// Erases `size` bytes of flash (whole erase blocks) and programs them with `data`, then reads them back. Retries up to
// PROGRAM_ATTEMPTS times.
bool program_flash_block(uint32_t flash_addr, uint8_t *data, uint32_t size) {
    const uint32_t page_size = flash_get_page_size(&FLASH_0);
    const uint32_t region_size = page_size * flash_get_total_pages(&FLASH_0) / FLASH_LOCK_REGIONS;

    if (_flash_is_locked(&FLASH_0.dev, flash_addr)) {
        flash_unlock(&FLASH_0, flash_addr & ~(region_size - 1), region_size / page_size);
    }

    for (uint32_t attempt = 0; attempt < PROGRAM_ATTEMPTS; attempt++) {
        if (flash_erase(&FLASH_0, flash_addr, size / page_size) != ERR_NONE) {
            continue;
        }
        // flash_append programs the erased pages; flash_write would copy the whole block onto the stack again
        if (flash_append(&FLASH_0, flash_addr, data, size) != ERR_NONE) {
            continue;
        }
        if (memcmp((const void *)flash_addr, data, size) == 0) {
            return true;
        }
    }
    return false;
}

// Turns off the CMCC and NVM caches so that reads after a flash write see the new contents
void disable_flash_caches(void) {
    cache_disable(CMCC);
    cache_invalidate_all(CMCC);
    hri_nvmctrl_set_CTRLA_CACHEDIS0_bit(NVMCTRL);
    hri_nvmctrl_set_CTRLA_CACHEDIS1_bit(NVMCTRL);
}

// Undoes disable_flash_caches, in reverse order
void enable_flash_caches(void) {
    hri_nvmctrl_clear_CTRLA_CACHEDIS1_bit(NVMCTRL);
    hri_nvmctrl_clear_CTRLA_CACHEDIS0_bit(NVMCTRL);
    cache_invalidate_all(CMCC);
    cache_enable(CMCC);
}
