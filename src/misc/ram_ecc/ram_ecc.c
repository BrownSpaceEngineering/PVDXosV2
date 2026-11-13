/**
 * ram_ecc.c
 *
 * Handles the SAMD51's RAM ECC errors (data sheet chapter 17). RAM ECC is on once the RAM ECCDIS bit (bit 39) of the NVM
 * user row is cleared. It then corrects single-bit errors and detects dual-bit errors on every read, and keeps its check
 * bits in the top half of RAM, so src_flash.ld and the bootloader only use the bottom 128 KB.
 *
 * Created: November 13, 2026
 * Authors: PVDX
 */

#include "ram_ecc.h"

#include <hpl_ramecc.h>

#include "atmel_start.h"
#include "logging.h"

volatile uint32_t ram_ecc_single_errors = 0;

// The read is corrected, but the flipped bit stays in RAM until the word is written again, so write the corrected value
// back. Reading that word raises the error again, so read ERRADDR afterwards to clear it.
static void ram_ecc_single_error(const uint32_t offset) {
    volatile uint32_t *const word = (volatile uint32_t *)(HSRAM_ADDR + (offset & ~3u));
    *word = *word;
    (void)hri_ramecc_read_ERRADDR_reg(RAMECC);

    ram_ecc_single_errors++;
    warning("ram_ecc: corrected a single-bit error at 0x%08lx\n", (unsigned long)(HSRAM_ADDR + offset));
}

// The data is wrong and there's no telling what used it
static void ram_ecc_dual_error(const uint32_t offset) {
    fatal("ram_ecc: uncorrectable error at 0x%08lx\n", (unsigned long)(HSRAM_ADDR + offset));
}

/**
 * \fn ram_ecc_init
 *
 * \brief Enables the RAM ECC error interrupts, if the RAM ECCDIS fuse has turned RAM ECC on
 */
void ram_ecc_init(void) {
    if (_ramecc_init() != ERR_NONE) {
        warning_impl("[!] RAM ECC is off (RAM ECCDIS fuse set)\n");
        return;
    }
    _ramecc_register_callback(RAMECC_SINGLE_ERROR_CB, ram_ecc_single_error);
    _ramecc_register_callback(RAMECC_DUAL_ERROR_CB, ram_ecc_dual_error);
    info_impl("[+] RAM ECC is on\n");
}
