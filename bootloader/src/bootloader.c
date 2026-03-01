#include "mram.h"

#define FLASH_OS_BASE_ADDRESS (0x00020000)  // Address of OS in flash (after bootloaders)
                                            // Note: in final version, OS copies will only be in MRAM
                                            // so this will be unused!

#define RAM_OS_BASE_ADDRESS (0x20000000)    // Where to load OS into RAM
#define BOOTLOADER_SIZE (0x3000)            // Size of each bootloader in the chain

#define SCS_BASE (0xE000E000UL)
#define SCB_BASE (SCS_BASE + 0x0D00UL)
#define SCB_VTOR (SCB_BASE + 0x08)          // VTOR: Vector Table Offset Register

#define RSTC_RCAUSE (0x40000C00UL)          // Reset Cause Register

int main(void);
void go_to_app(void);

int main(void) {
    uint8_t bootloader_index = 255;
#ifdef BOOTLOADER_1
    bootloader_index = 0;
#endif
#ifdef BOOTLOADER_2
    bootloader_index = 1;
#endif
#ifdef BOOTLOADER_3
    bootloader_index = 2;
#endif
    // If no bootloader flag was defined, panic
    while (bootloader_index == 255);

    uint32_t checksum = 0;
    volatile uint8_t *cur_bootloader_start = (uint8_t *)(bootloader_index * BOOTLOADER_SIZE);
    for (uint32_t i = 0; i < BOOTLOADER_SIZE; i++) {
        checksum += cur_bootloader_start[i];
    }
    checksum %= 256;

    if (checksum != 0) {
        // If checksum fails on the last bootloader, there is nowhere to jump so panic
        while (bootloader_index == 2);

        uint32_t next_sp = *(uint32_t *)((bootloader_index + 1) * BOOTLOADER_SIZE);
        uint32_t next_pc = *(uint32_t *)((bootloader_index + 1) * BOOTLOADER_SIZE + 0x0004);
        __asm__ volatile(
            "mov sp, %0\n" // Move the value in desired_sp into SP
            "bx %1"        // Branch to the address contained in desired_pc
            :
            : "r"(next_sp), "r"(next_pc) // Arguments to the assembly (accessed as %0 and %1 in the assembly code)
            :);
    }

#if defined(MRAM_OS_WRITE) || defined(MRAM_OS_READ)
    mram_init();
#endif

    char *os_flash_src = (char *)FLASH_OS_BASE_ADDRESS;
    char *os_dst = (char *)RAM_OS_BASE_ADDRESS;

#ifdef MRAM_OS_WRITE
    mram_write_bytes(MRAM_OS_BASE_ADDRESS, (uint8_t *)os_flash_src, MRAM_OS_SIZE);
    mram_write_bytes(MRAM_FLASH_BASE_ADDRESS, (uint8_t *)0x00000000, MRAM_FLASH_SIZE);
#endif

#ifdef MRAM_OS_READ
    mram_read_bytes(MRAM_OS_BASE_ADDRESS, (uint8_t *)os_dst, MRAM_OS_SIZE);
#else
    for (long i = 0; i < MRAM_OS_SIZE; i++) {
        os_dst[i] = os_flash_src[i];
    }
#endif

    go_to_app();
    __builtin_unreachable();
}

void go_to_app(void) {
    // Read app's vector table (first value is SP, second value is PC)
    long *vector_table = (long *)RAM_OS_BASE_ADDRESS;
    long desired_sp = vector_table[0];
    long desired_pc = vector_table[1];

    // Set the least significant bit of the PC to indicate that the reset vector is in Thumb mode
    desired_pc |= 0x1;

    // Tell the SAMD51 where the app's vector table is
    *((long *)SCB_VTOR) = (long)vector_table;

    // Set SP to desired_sp and then jump to PC using assembly
    __asm__ volatile(
        "mov sp, %0\n" // Move the value in desired_sp into SP
        "bx %1"        // Branch to the address contained in desired_pc
        :
        : "r"(desired_sp), "r"(desired_pc) // Arguments to the assembly (accessed as %0 and %1 in the assembly code)
        :);
}