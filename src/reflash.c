#include "atmel_start.h"

extern struct flash_descriptor FLASH_0;

#define BLOCK_SIZE 8192

void reflash_bootloaders(void) {
    // this stops it working! wow
    flash_erase(&FLASH_0, 0, 1);
}