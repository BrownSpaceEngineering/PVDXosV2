extern char _sdata;   // Start of .data in RAM
extern char _edata;   // End of .data in RAM
extern char _sidata;  // Start of .data in FLASH

extern char _sbss;    // Start of .bss in RAM
extern char _ebss;    // End of .bss in RAM

extern void bootloader(void);

void startup(void);

// The vectors section will be placed at address 0 in flash based on the linker script
__attribute__((section(".vectors"))) const long startup_vectors[] = {
    0x2003FFFC,     // Initial (bootloader's) SP
    (long)startup   // Initial PC
};

// Responsible for initializing global variables for the bootloader
// As such this function must not reference global variables
void startup(void) {

    #ifdef BOOTLOADER_1
        const volatile char* checksum_ptr = (char*)0x9000;
        long sum = 0;
        for (volatile char* mem = (char*)0x0000; mem < (char*)0x3000; mem++) {
            sum += *mem;
        }
        sum %= 256;

        long new_pc = *(long*)0x3004;

        if (*checksum_ptr != (char)sum) {
            // jump to second bootloader
            __asm__ volatile("mov sp, %0\n" // Move the value in desired_sp into SP
                     "bx %1"        // Branch to the address contained in desired_pc
                     :
                     : "r"(0x2003FFFC), "r"(new_pc) // Arguments to the assembly (accessed as %0 and %1 in the assembly code)
                     :);
            __builtin_unreachable();
        }
    #elif BOOTLOADER_2
        const volatile char* checksum_ptr = (char*)0x9004;
        long sum = 0;
        for (volatile char* mem = (char*)0x3000; mem < (char*)0x6000; mem++) {
            sum += *mem;
        }
        sum %= 256;

        long new_pc = *(long*)0x6004;

        if (*checksum_ptr != (char)sum) {
            // jump to second bootloader
            __asm__ volatile("mov sp, %0\n" // Move the value in desired_sp into SP
                     "bx %1"        // Branch to the address contained in desired_pc
                     :
                     : "r"(0x2003FFFC), "r"(new_pc) // Arguments to the assembly (accessed as %0 and %1 in the assembly code)
                     :);
            __builtin_unreachable();
        }
    #endif

    char *src, *dst;

    // Copy bootloader's data segment
    src = &_sidata;
    dst = &_sdata;
    while (dst < &_edata) {
        *dst = *src;
        src++;
        dst++;
    }

    // Zero out bootloader's BSS segment
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst = 0;
        dst++;
    }

    bootloader();
}