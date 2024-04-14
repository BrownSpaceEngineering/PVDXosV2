
# Overview
The bootloader is a completely separate program from the main PVDXos application. It is compiled separately and PVDX has no knowledge of it. The bootloader is responsible for error-checking the PVDX .text section, loading the main PVDXos application into memory, and then transferring control to PVDX.

## Bootloader Design
The bootloader is designed to be as small and simple as possible. It is not linked with libc, and does not use any external libraries. 

The SAMD51 looks at the first two words of flash (address 0x0 and 0x4) to determine the stack pointer (at 0x0) and program counter (at 0x4).

startup.s is the entry point for the bootloader, meaning its address is stored at 0x4. Startup.s sets up the .data section and .bss section. Once the runtime environment is initialized, it then calls the bootloader() function in bootloader.c

bootloader.c is the main file for the bootloader. It is responsible for error-checking the PVDX .text section, loading the main PVDXos application into memory, and then transferring control to PVDX.

When transferring control to PVDX, the bootloader reads the first two words of the PVDX application to determine the stack pointer and program counter. It then sets the stack pointer and program counter to these values, and jumps to the program counter. Essentially, this is emulating what the hardware would do, so PVDX remains unaware of a bootloader's existence.
