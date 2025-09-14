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
    // if (startup_vectors == 0x3000)

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