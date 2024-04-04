#include "bootloader.h"

#include <stdint.h>

__attribute__((noreturn)) void bootloader() {

    // Check the reset cause
    uint8_t *p_rcause = (uint8_t *)RSTC_RCAUSE;
    uint8_t rcause = *p_rcause;

    if (rcause == 0x20) {
        // Watchdog reset happened, maybe worth doing something about later?
        // Wipe all memory (VERY RISKY THIS TOTALLY IS UB)
        for (register int *p = (int *)0x20000000; p < (int *)0x20040000; p++) {
            *p = 0;
        }
    }

    // write magic number to backup RAM to indicate bootloader has ran successfully
    uint32_t *p_magic_number = (uint32_t *)MAGIC_NUMBER_ADDRESS;
    *p_magic_number = MAGIC_NUMBER;

    // Done with bootloader: transfer control to PVDX application
    transfer_to_application();
}

__attribute__((noreturn)) void transfer_to_application() {
    // Read PVDX's exception table to find the PVDX reset vector (First byte is SP, second is PC)
    uintptr_t desired_sp = (*(uint32_t *)APPLICATION_START_ADDRESS);
    uintptr_t desired_pc = *(uint32_t *)(APPLICATION_START_ADDRESS + 4);

    if (desired_pc < APPLICATION_START_ADDRESS || desired_pc > FLASH_END) {
        // PC is not within flash
        // TODO fail in some way
    }
    if (desired_sp < RAM_START || desired_sp > RAM_END) {
        // SP is not within RAM
        // TODO fail in some way
    }

    // tell the SAMD51 that the exception table is at the start of PVDX (at APPLICATION_START_ADDRESS)
    uint32_t *p_scb_vtor = (uint32_t *)SCB_VTOR;
    *p_scb_vtor = (APPLICATION_START_ADDRESS & SCB_VTOR_TBLOFF_Msk);

    // set SP to desired_sp and then jump to PC using assembly
    __asm__ volatile("mov sp, %0\n" // Move the value in desired_sp into SP
                     "bx %1"        // Branch to the address contained in desired_pc
                     :
                     : "r"(desired_sp), "r"(desired_pc) // Arguments to the assembly (accessed as %0 and %1 in the assembly code)
                     :);
    // Marker to tell the compiler that we never get here
    __builtin_unreachable();
}