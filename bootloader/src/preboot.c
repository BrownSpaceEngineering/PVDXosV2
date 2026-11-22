#include <stdbool.h>
#include <stdint.h>

#include "boot_record.h"
#include "image_format.h"

#define SCB_VTOR (*(volatile uint32_t *)0xE000ED08u)
#define SCB_AIRCR (*(volatile uint32_t *)0xE000ED0Cu)
#define SCB_AIRCR_SYSRESETREQ (0x05FA0004u)
#define RAM_START (0x20000000u)
#define RAM_END (0x20040000u)

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

// Starts this boot's record; stage 2 fills in the rest. Volatile word stores: no libc memset under -nostdlib.
static void start_boot_record(uint8_t stage2_slot) {
    volatile uint32_t *words = (volatile uint32_t *)BOOT_RECORD_ADDR;
    for (uint32_t i = 0; i < sizeof(boot_record_t) / sizeof(uint32_t); i++) {
        words[i] = 0u;
    }
    volatile boot_record_t *record = (volatile boot_record_t *)BOOT_RECORD_ADDR;
    record->stage2_slot = stage2_slot;
    record->magic = BOOT_RECORD_MAGIC;
}

static bool vector_table_plausible(uint32_t address, uint32_t code_end) {
    const uint32_t *vectors = (const uint32_t *)address;
    uint32_t stack_pointer = vectors[0];
    uint32_t reset_handler = vectors[1];
    return stack_pointer > RAM_START && stack_pointer <= RAM_END && (stack_pointer & 3u) == 0u && (reset_handler & 1u) != 0u &&
        (reset_handler & ~1u) >= address && (reset_handler & ~1u) < code_end;
}

static void jump_to(uint32_t address) {
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
        uint32_t address = FLASH_STAGE2_BASE + index * FLASH_STAGE2_SIZE;
        const uint8_t *image = (const uint8_t *)address;
        const uint32_t *stored_checksum = (const uint32_t *)(address + FLASH_STAGE2_SIZE - sizeof(uint32_t));

        if (crc32(image, FLASH_STAGE2_SIZE - sizeof(uint32_t)) == *stored_checksum) {
            start_boot_record((uint8_t)(index + 1u));
            jump_to(address);
        }
    }

    // Every stage-2 copy is damaged: run the OS in place without MRAM repair rather than stopping here.
    start_boot_record(BOOT_STAGE2_SLOT_FALLBACK);
    if (vector_table_plausible(FLASH_OS_APP_ADDR, FLASH_OS_ADDR + FLASH_OS_SIZE)) {
        jump_to(FLASH_OS_APP_ADDR);
    }

    // The OS image is not bootable either. The watchdog is off at reset, so reset explicitly and try again.
    SCB_AIRCR = SCB_AIRCR_SYSRESETREQ;
    while (1) {}
}

extern void *_estack;

__attribute__((section(".vectors"), used)) void *const vector_table[] = {&_estack, (void *)main};
