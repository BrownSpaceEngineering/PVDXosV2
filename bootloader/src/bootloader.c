#include "mram_driver.h"
#include "watchdog_driver.h"

#define FLASH_OS_BASE_ADDRESS (0x020000) // Address of OS in flash (after bootloaders)
#define RAM_OS_BASE_ADDRESS (0x20000000) // Where to load OS into RAM

#define SCS_BASE (0xE000E000UL)
#define SCB_BASE (SCS_BASE + 0x0D00UL)
#define SCB_VTOR (SCB_BASE + 0x08) // VTOR: Vector Table Offset Register

#define RSTC_RCAUSE (0x40000C00UL) // Reset Cause Register

int main(void);
void go_to_app(void);

bool mram_checksum_matches(const uint8_t *os_address) {
    const uint8_t *checksum = os_address + MRAM_OS_SIZE - sizeof(uint32_t);
    uint32_t stored_checksum = ((uint32_t)checksum[0] << 0)
                             | ((uint32_t)checksum[1] << 8)
                             | ((uint32_t)checksum[2] << 16)
                             | ((uint32_t)checksum[3] << 24);
    return crc32(os_address, MRAM_OS_SIZE - sizeof(uint32_t)) == stored_checksum;
}

int main(void) {
    watchdog_setup();

    mram_status_t mram_status = mram_init();

    bool mram_ok = (get_mram_state(mram_status, 1) == MRAM_STATE_OK)
                || (get_mram_state(mram_status, 2) == MRAM_STATE_OK)
                || (get_mram_state(mram_status, 3) == MRAM_STATE_OK);

    char *os_dst = (char *)RAM_OS_BASE_ADDRESS;

// only for debugging; will not be present in final
#ifdef WRITE_FLASH_TO_MRAM
    if (mram_ok) {
        mram_write_bytes(MRAM_FLASH_BASE_ADDRESS, (uint8_t *)0x00000000, MRAM_FLASH_SIZE);
    }
#endif

    if (mram_ok) {
        mram_read_bytes(MRAM_OS_BASE_ADDRESS + 0 * MRAM_OS_SIZE, (uint8_t *)os_dst, MRAM_OS_SIZE);
        if (mram_checksum_matches((uint8_t *)os_dst)) {
            go_to_app();
        }
        mram_read_bytes(MRAM_OS_BASE_ADDRESS + 1 * MRAM_OS_SIZE, (uint8_t *)os_dst, MRAM_OS_SIZE);
        if (mram_checksum_matches((uint8_t *)os_dst)) {
            go_to_app();
        }
        mram_read_bytes(MRAM_OS_BASE_ADDRESS + 2 * MRAM_OS_SIZE, (uint8_t *)os_dst, MRAM_OS_SIZE);
        if (mram_checksum_matches((uint8_t *) os_dst)) {
            go_to_app();
        }
    } else {
        char *os_flash_src = (char *)FLASH_OS_BASE_ADDRESS;
        for (long i = 0; i < MRAM_OS_SIZE; i++) {
            os_dst[i] = os_flash_src[i];
        }
    }

    go_to_app();
    __builtin_unreachable();
}

void go_to_app(void) {
    // Read app's vector table (first value is SP, second value is PC)
    long *vector_table = (long *)RAM_OS_BASE_ADDRESS;
    long desired_sp = vector_table[0];
    long desired_pc = vector_table[1];

    // Set the least significant bit of the PC to indicate that the reset vector is in Thumb mode
    desired_pc |= 0x1;

    // Tell the SAMD51 where the app's vector table is
    *((long *)SCB_VTOR) = (long)vector_table;

    // Set SP to desired_sp and then jump to PC using assembly
    __asm__ volatile(
        "mov sp, %0\n" // Move the value in desired_sp into SP
        "bx %1"        // Branch to the address contained in desired_pc
        :
        : "r"(desired_sp), "r"(desired_pc) // Arguments to the assembly (accessed as %0 and %1 in the assembly code)
        :);
}