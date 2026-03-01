#include "atmel_start.h"
#include "mram.h"

extern struct flash_descriptor FLASH_0;

#define FLASH_BLOCK_SIZE 8192

void reflash_bootloaders(void) {
    uint8_t mram_block[FLASH_BLOCK_SIZE];
    uint32_t block_count = MRAM_FLASH_SIZE / FLASH_BLOCK_SIZE;

    for (uint32_t i = 0; i < block_count; i++) {
        mram_read_bytes(MRAM_FLASH_BASE_ADDRESS + i * FLASH_BLOCK_SIZE, mram_block, FLASH_BLOCK_SIZE);

        uint8_t* flash_block = (uint8_t*)(i * FLASH_BLOCK_SIZE);

        bool any_error = false;
        for (uint32_t j = 0; j < FLASH_BLOCK_SIZE; j++) {
            if (mram_block[j] != flash_block[j]) {
                any_error = true;
                break;
            }
        }

        if (any_error) {
            flash_erase(&FLASH_0, i * FLASH_BLOCK_SIZE, 1);
            flash_write(&FLASH_0, i * FLASH_BLOCK_SIZE, mram_block, FLASH_BLOCK_SIZE);
        }
    }
}