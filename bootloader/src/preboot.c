#include <stdint.h>

#define STAGE2_COUNT 8
#define STAGE2_SIZE 0x2000u

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
    for (uint32_t index = 0; index < STAGE2_COUNT; index++) {
        uint32_t address = (index + 1u) * STAGE2_SIZE;
        const uint8_t *image = (const uint8_t *)address;
        const uint32_t *stored_checksum = (const uint32_t *)(address + STAGE2_SIZE - sizeof(uint32_t));

        if (crc32(image, STAGE2_SIZE - sizeof(uint32_t)) == *stored_checksum) {
            jump_to_stage2(address);
        }
    }

    while (1) {} // Should be unreachable.
}

extern void *_estack;

__attribute__((section(".vectors"), used)) void *const vector_table[] = {&_estack, (void *)main};