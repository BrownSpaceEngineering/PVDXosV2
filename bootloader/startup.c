extern char _sdata;   // Start of .data in RAM
extern char _edata;   // End of .data in RAM
extern char _sidata;  // Start of .data in FLASH

extern char _sbss;    // Start of .bss in RAM
extern char _ebss;    // End of .bss in RAM

extern void bootloader(void);

void startup(void);

__attribute__((section(".vectors"))) const long startup_vectors[] = {0x2003FFFC, (long)startup};

void startup(void) {
    char *p_src, *p_dst;

    // Copy bootloader's data segment
    p_src = &_sidata;
    p_dst = &_sdata;
    while (p_dst < &_edata) {
        *p_dst = *p_src;
        p_src++;
        p_dst++;
    }

    // Zero out bootloader's BSS segment
    p_dst = &_sbss;
    while (p_dst < &_ebss) {
        *p_dst = 0;
        p_dst++;
    }

    bootloader();
}