#include "flash_driver.h"

#include <string.h>

#include "atmel_start.h"
#include "hri_cmcc_d51.h"
#include "hri_nvmctrl_d51.h"
#include "watchdog_driver.h"

#define FLASH_LOCK_REGION_COUNT (32u)
#define FLASH_LOCK_REGION_SIZE (0x00008000u)
#define FLASH_PROGRAM_ATTEMPTS (3u)
#define NVM_ERROR_FLAGS                                                                                                                    \
    (NVMCTRL_INTFLAG_ADDRE | NVMCTRL_INTFLAG_PROGE | NVMCTRL_INTFLAG_LOCKE | NVMCTRL_INTFLAG_ECCSE | NVMCTRL_INTFLAG_ECCDE |               \
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

static bool program_unlocked_bootloader_block(uint32_t address, const uint8_t *data) {
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

bool flash_program_block(uint32_t address, const uint8_t *data) {
    bool is_os_row = (address >= FLASH_OS_ADDR) && ((address + FLASH_REPAIR_BLOCK_SIZE) <= (FLASH_OS_ADDR + FLASH_OS_SIZE));
    if (!is_os_row || (address % FLASH_REPAIR_BLOCK_SIZE) != 0u || data == NULL || !flash_unlock_address(address)) {
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

static bool bootloader_address_valid(uint32_t address) {
    return address < FLASH_STAGE2_BASE + FLASH_STAGE2_COUNT * FLASH_STAGE2_SIZE &&
        address % FLASH_STAGE2_SIZE == 0u;
}

typedef struct {
    uint16_t ctrla;
    uint16_t interrupts;
    bool cmcc_enabled;
} flash_access_state_t;

static flash_access_state_t bootloader_access_begin(void) {
    nvm_wait_ready();
    flash_access_state_t state = {
        .ctrla = hri_nvmctrl_read_CTRLA_reg(NVMCTRL),
        .interrupts = NVMCTRL->INTENSET.reg,
        .cmcc_enabled = hri_cmcc_get_SR_CSTS_bit(CMCC),
    };
    // Prevent an NVM IRQ from consuming ECC evidence while polling it ourselves.
    NVMCTRL->INTENCLR.reg = NVMCTRL_INTENCLR_MASK;
    flash_disable_caches();
    while (hri_cmcc_get_SR_CSTS_bit(CMCC)) {}
    __DSB();
    __ISB();
    (void)get_eccerr_status(); // Discard errors from before this slot's read.
    hri_nvmctrl_clear_INTFLAG_reg(NVMCTRL, NVMCTRL_INTFLAG_ECCSE | NVMCTRL_INTFLAG_ECCDE);
    return state;
}

static void bootloader_access_end(flash_access_state_t state) {
    hri_nvmctrl_clear_INTFLAG_reg(NVMCTRL, NVMCTRL_INTFLAG_ECCSE | NVMCTRL_INTFLAG_ECCDE);
    hri_cmcc_write_MAINT0_reg(CMCC, CMCC_MAINT0_INVALL);
    hri_nvmctrl_write_CTRLA_reg(NVMCTRL, state.ctrla);
    if (state.cmcc_enabled) {
        hri_cmcc_write_CTRL_reg(CMCC, CMCC_CTRL_CEN);
    }
    __DSB();
    __ISB();
    NVMCTRL->INTENSET.reg = state.interrupts;
}

bool flash_read_bootloader_block(uint32_t address, uint8_t *data) {
    if (!bootloader_address_valid(address) || data == NULL) {
        return false;
    }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    flash_access_state_t state = bootloader_access_begin();
    // Volatile loads also support the preboot slot at address zero.
    const volatile uint32_t *words = (const volatile uint32_t *)address;
    for (uint32_t offset = 0; offset < FLASH_STAGE2_SIZE; offset += sizeof(uint32_t)) {
        uint32_t word = words[offset / sizeof(uint32_t)];
        memcpy(data + offset, &word, sizeof(word));
    }
    __DSB();
    uint32_t ecc_status = get_eccerr_status();
    bool okay = (ecc_status & (NVMCTRL_ECCERR_TYPEL_Msk | NVMCTRL_ECCERR_TYPEH_Msk)) == 0u;
    bootloader_access_end(state);
    __set_PRIMASK(primask);
    return okay;
}

bool flash_program_bootloader_block(uint32_t address, const uint8_t *data) {
    if (!bootloader_address_valid(address) || data == NULL) {
        return false;
    }
    flash_access_state_t state = bootloader_access_begin();
    uint32_t region_address = (address / FLASH_LOCK_REGION_SIZE) * FLASH_LOCK_REGION_SIZE;
    bool was_locked = (hri_nvmctrl_read_RUNLOCK_reg(NVMCTRL) & (1u << (address / FLASH_LOCK_REGION_SIZE))) == 0u;
    uint16_t status = hri_nvmctrl_read_STATUS_reg(NVMCTRL);
    uint32_t protected_size = (15u - ((status & NVMCTRL_STATUS_BOOTPROT_Msk) >> NVMCTRL_STATUS_BOOTPROT_Pos)) * IMAGE_BLOCK_SIZE;
    bool disable_bootprot = address < protected_size && (status & NVMCTRL_STATUS_BPDIS) == 0u;
    bool okay = !disable_bootprot || nvm_command(address, NVMCTRL_CTRLB_CMD_SBPDIS);
    hri_nvmctrl_write_CTRLA_reg(
        NVMCTRL, (hri_nvmctrl_read_CTRLA_reg(NVMCTRL) & ~(NVMCTRL_CTRLA_WMODE_Msk | NVMCTRL_CTRLA_SUSPEN)) | NVMCTRL_CTRLA_WMODE_MAN);
    if (okay) {
        okay = flash_unlock_address(address) && program_unlocked_bootloader_block(address, data);
    }
    uint32_t ecc_status = get_eccerr_status();
    okay = okay && (ecc_status & (NVMCTRL_ECCERR_TYPEL_Msk | NVMCTRL_ECCERR_TYPEH_Msk)) == 0u;
    if (was_locked && !nvm_command(region_address, NVMCTRL_CTRLB_CMD_LR)) {
        okay = false;
    }
    if (disable_bootprot && !nvm_command(address, NVMCTRL_CTRLB_CMD_CBPDIS)) {
        okay = false;
    }
    bootloader_access_end(state);
    return okay;
}

uint32_t get_eccerr_status(void) {
    return (uint32_t)hri_nvmctrl_read_ECCERR_reg(NVMCTRL);
}