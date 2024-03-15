// For now, just a minimal bootloader that checks nothing and defers to the main application.
// In order to check that it worked, increments some counter in backup RAM and then jumps to the main application.
#include "bootloader.h"

#include <stdint.h>

int bootloader() {
    // write magic number to backup RAM
    uint32_t *magic_number_addr = (uint32_t *)MAGIC_NUMBER_ADDRESS;
    *magic_number_addr = MAGIC_NUMBER;

    while (1) {}
}