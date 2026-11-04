#include <stdint.h>

#include "image_format.h"

// Stage 1 is the only code at the reset vector, so it stays tiny: it jumps to the first stage-2 copy
// whose CRC32 (stored in the last 4 bytes of its slot) is valid. Stage 2 then checks and repairs flash.

#define SCB_VTOR (*(volatile uint32_t *)0xE000ED08u)

static uint32_t crc32(const uint8_t *data, uint32_t size) {
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    }

    return crc ^ 0xFFFFFFFFu;
}

static void jump_to_stage2(uint32_t address) {
    const uint32_t *vectors = (const uint32_t *)address;
    uint32_t stack_pointer = vectors[0];
    uint32_t reset_handler = vectors[1] | 1u;

    SCB_VTOR = address;
    __asm__ volatile(
        "mov sp, %0\n"
        "bx %1"
        :
        : "r"(stack_pointer), "r"(reset_handler)
        : "memory");
    __builtin_unreachable();
}

int main(void) {
    for (uint32_t index = 0; index < FLASH_STAGE2_COUNT; index++) {
        uint32_t address = FLASH_STAGE2_ADDR(index);
        const uint8_t *image = (const uint8_t *)address;
        const uint32_t *stored_checksum = (const uint32_t *)(address + FLASH_STAGE2_SIZE - sizeof(uint32_t));

        if (crc32(image, FLASH_STAGE2_SIZE - sizeof(uint32_t)) == *stored_checksum) {
            jump_to_stage2(address);
        }
    }

    while (1) {} // Should be unreachable unless every bootloader has failed.
}

extern void *_estack;

__attribute__((section(".vectors"), used)) void *const vector_table[] = {&_estack, (void *)main};
