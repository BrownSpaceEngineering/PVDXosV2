#include "mram.h"

#define APP_FLASH_START (0x00020000) // Change based on where your app is stored
#define APP_FLASH_STEP (0x00020000)  // Step to next copy of app in flash

#define APP_RAM_START (0x20000000)   // Starting RAM address for the app
#define APP_SIZE (0x20000)           // Size of the app in bytes

#define SCS_BASE (0xE000E000UL)
#define SCB_BASE (SCS_BASE + 0x0D00UL)
#define SCB_VTOR (SCB_BASE + 0x08)  // VTOR: Vector Table Offset Register (where the processor will reset from)
#define SCB_AIRCR (SCB_BASE + 0x0C) // AIRCR Contains SYSRESETREQ bit to request a system reset

// #define SCB_VTOR_TBLOFF_Msk (0xFFFFFF80UL) // Mask off the last 7 bits (128 bytes alignment)

#define SCB_AIRCR_VECTKEY_Pos 16U
#define SCB_AIRCR_SYSRESETREQ_Pos 2U
#define SCB_AIRCR_SYSRESETREQ_Msk (1UL << SCB_AIRCR_SYSRESETREQ_Pos)

#define RSTC_RCAUSE (0x40000C00UL + 0x00UL) // Reset Cause Register

#define BOOTLOADER_SIZE 0x3000
#define BOOTLOADER_INITIAL_SP 0x2003FFFC

int main(void);
void go_to_app(void);

volatile int startup_test_value = 8;

// #define MRAM_OS_WRITE
// #define MRAM_OS_READ

int main(void) {
    // This loop will spin forever if startup did not copy data segment
    while (startup_test_value != 8);

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

        uint32_t next_pc = *(uint32_t *)((bootloader_index + 1) * BOOTLOADER_SIZE + 0x0004);
        __asm__ volatile(
            "mov sp, %0\n" // Move the value in desired_sp into SP
            "bx %1"        // Branch to the address contained in desired_pc
            :
            : "r"(BOOTLOADER_INITIAL_SP), "r"(next_pc) // Arguments to the assembly (accessed as %0 and %1 in the assembly code)
            :);
    }

#if defined(MRAM_OS_WRITE) || defined(MRAM_OS_READ)
    mram_init();
#endif

    char *app_flash_src = (char *)APP_FLASH_START;
    char *app_dst = (char *)APP_RAM_START;

#ifdef MRAM_OS_WRITE
    // Copy application from flash to RAM

    mram_write_bytes(0x100, (uint8_t *)app_flash_src, APP_SIZE);
#endif

#ifdef MRAM_OS_READ
    mram_read_bytes(0x100, (uint8_t *)app_dst, APP_SIZE);
#else
    for (long i = 0; i < APP_SIZE; i++) {
        // dst[i] = src[i];
        // take the majority (if at least one AND pair evaluates to 1 then it should be 1)
        app_dst[i] = (app_flash_src[i] & app_flash_src[i+APP_FLASH_STEP]) |
            (app_flash_src[i+APP_FLASH_STEP] & app_flash_src[i+2*APP_FLASH_STEP]) |
            (app_flash_src[i+2*APP_FLASH_STEP] & app_flash_src[i]);
    }
#endif

    go_to_app();
    __builtin_unreachable();
}

void go_to_app(void) {
    // Read app's vector table (first value is SP, second value is PC)
    long *vector_table = (long *)APP_RAM_START;
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