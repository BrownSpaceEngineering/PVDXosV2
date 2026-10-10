## Bootloader

This is the bootloader for PVDXos. It runs whenever the CPU turns on/reboots, and does the work of initializing everything the main OS needs to run. More specifically:

- The `.ld` files dictate the memory layout of the program for the linker.
- The `startup.c` file runs first, providing the bootloader's initial stack pointer and program counter (the address of the first instruction to run) which the CPU will then read to start executing.
- It also does some fun redundancy protection by checking the integrity of the bootloader code and deciding whether to jump to the next bootloader if there is an issue with this one.
- Finally, we execute the main bootloader code in `bootloader.c`, which copies the PVDXos code from flash memory to RAM, and jumps to it, transferring control to the main OS from the top-level `src` folder.
