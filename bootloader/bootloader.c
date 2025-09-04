#define APP_FLASH_START (0x00010000) // Change based on where your app is stored
#define APP_RAM_START (0x20000000)   // Starting RAM address for the app
#define RAM_SIZE (0x3E000)           // Size of the app in bytes

#define SCS_BASE (0xE000E000UL)
#define SCB_BASE (SCS_BASE + 0x0D00UL)
#define SCB_VTOR (SCB_BASE + 0x08)  // VTOR: Vector Table Offset Register (where the processor will reset from)
#define SCB_AIRCR (SCB_BASE + 0x0C) // AIRCR Contains SYSRESETREQ bit to request a system reset

#define SCB_VTOR_TBLOFF_Msk (0xFFFFFF80UL) // Mask off the last 7 bits (128 bytes alignment)

#define SCB_AIRCR_VECTKEY_Pos 16U
#define SCB_AIRCR_SYSRESETREQ_Pos 2U
#define SCB_AIRCR_SYSRESETREQ_Msk (1UL << SCB_AIRCR_SYSRESETREQ_Pos)

#define RSTC_RCAUSE (0x40000C00UL + 0x00UL) // Reset Cause Register

void bootloader(void);
void go_to_app(void);

volatile int test = 8;

void bootloader(void) {
    while (test != 8);

    // Copy application from flash to RAM
    char *src = (char *)APP_FLASH_START;
    char *dst = (char *)APP_RAM_START;
    for (long i = 0; i < RAM_SIZE; i++) {
        dst[i] = src[i];
    }
    
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
    __asm__ volatile("mov sp, %0\n" // Move the value in desired_sp into SP
                     "bx %1"        // Branch to the address contained in desired_pc
                     :
                     : "r"(desired_sp), "r"(desired_pc) // Arguments to the assembly (accessed as %0 and %1 in the assembly code)
                     :);
}