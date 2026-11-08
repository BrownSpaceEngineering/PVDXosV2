#include "flash_driver.h"

#include <string.h>

#include "atmel_start.h"
#include "hri_cmcc_d51.h"
#include "hri_nvmctrl_d51.h"
#include "watchdog_driver.h"

#define FLASH_LOCK_REGION_COUNT (32u)
#define FLASH_LOCK_REGION_SIZE (0x00008000u)
#define FLASH_PROGRAM_ATTEMPTS (3u)
#define NVM_ERROR_FLAGS (NVMCTRL_INTFLAG_ADDRE | NVMCTRL_INTFLAG_PROGE | NVMCTRL_INTFLAG_LOCKE | NVMCTRL_INTFLAG_ECCSE | \
                         NVMCTRL_INTFLAG_NVME | NVMCTRL_INTFLAG_SEESOVF)

static bool nvm_wait_ready(void) {
    while (!hri_nvmctrl_get_STATUS_READY_bit(NVMCTRL)) {
        watchdog_pet();
    }

    return true;
}

static bool nvm_command(uint32_t address, uint16_t command) {
    nvm_wait_ready();
    hri_nvmctrl_clear_INTFLAG_reg(NVMCTRL, NVM_ERROR_FLAGS | NVMCTRL_INTFLAG_DONE);
    hri_nvmctrl_write_ADDR_reg(NVMCTRL, address);
    hri_nvmctrl_write_CTRLB_reg(NVMCTRL, command | NVMCTRL_CTRLB_CMDEX_KEY);
    nvm_wait_ready();

    const uint16_t errors = hri_nvmctrl_read_INTFLAG_reg(NVMCTRL) & NVM_ERROR_FLAGS;
    if (errors != 0u) {
        hri_nvmctrl_clear_INTFLAG_reg(NVMCTRL, errors);
        return false;
    }

    return true;
}

static bool flash_unlock_address(uint32_t address) {
    const uint32_t region_index = address / FLASH_LOCK_REGION_SIZE;
    if (region_index >= FLASH_LOCK_REGION_COUNT) {
        return false;
    }

    const uint32_t region_mask = 1u << region_index;
    if ((hri_nvmctrl_read_RUNLOCK_reg(NVMCTRL) & region_mask) == 0u) {
        return nvm_command(region_index * FLASH_LOCK_REGION_SIZE, NVMCTRL_CTRLB_CMD_UR);
    }

    return true;
}

static bool flash_erase_block(uint32_t address) {
    return nvm_command(address, NVMCTRL_CTRLB_CMD_EB);
}

static bool flash_program_page(uint32_t address, const uint8_t *data) {
    if (!nvm_command(0u, NVMCTRL_CTRLB_CMD_PBC)) {
        return false;
    }

    volatile uint32_t *flash_words = (volatile uint32_t *)address;
    for (uint32_t offset = 0; offset < FLASH_REPAIR_PAGE_SIZE; offset += sizeof(uint32_t)) {
        uint32_t word;
        memcpy(&word, data + offset, sizeof(word));
        flash_words[offset / sizeof(uint32_t)] = word;
    }

    nvm_wait_ready();
    return nvm_command(address, NVMCTRL_CTRLB_CMD_WP);
}

void flash_disable_caches(void) {
    hri_cmcc_write_CTRL_reg(CMCC, 0u);
    hri_cmcc_write_MAINT0_reg(CMCC, CMCC_MAINT0_INVALL);
    hri_nvmctrl_set_CTRLA_CACHEDIS0_bit(NVMCTRL);
    hri_nvmctrl_set_CTRLA_CACHEDIS1_bit(NVMCTRL);
}

void flash_enable_caches(void) {
    hri_nvmctrl_clear_CTRLA_CACHEDIS1_bit(NVMCTRL);
    hri_nvmctrl_clear_CTRLA_CACHEDIS0_bit(NVMCTRL);
    hri_cmcc_write_MAINT0_reg(CMCC, CMCC_MAINT0_INVALL);
    hri_cmcc_write_CTRL_reg(CMCC, CMCC_CTRL_CEN);
}

bool flash_program_block(uint32_t address, const uint8_t *data) {
    bool is_os_row = address >= FLASH_OS_ADDR && address + FLASH_REPAIR_BLOCK_SIZE <= FLASH_OS_ADDR + FLASH_OS_SIZE;
    if (!is_os_row || (address % FLASH_REPAIR_BLOCK_SIZE) != 0u || data == NULL ||
        !flash_unlock_address(address)) {
        return false;
    }

    for (uint32_t attempt = 0; attempt < FLASH_PROGRAM_ATTEMPTS; attempt++) {
        if (!flash_erase_block(address)) {
            continue;
        }
        bool programmed = true;
        for (uint32_t offset = 0; offset < FLASH_REPAIR_BLOCK_SIZE; offset += FLASH_REPAIR_PAGE_SIZE) {
            if (!flash_program_page(address + offset, data + offset)) {
                programmed = false;
                break;
            }
        }

        if (programmed && memcmp((const void *)address, data, FLASH_REPAIR_BLOCK_SIZE) == 0) {
            return true;
        }
    }
    return false;
}
