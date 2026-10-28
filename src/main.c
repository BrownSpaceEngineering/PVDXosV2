/**
 * main.c
 *
 * The main entry point for PVDXos. This file initializes the hardware and verifies that the bootloader executed
 * successfully. It then reads a boot counter from MRAM, increments it, writes it back, prints it and idles forever
 * without ever starting the scheduler.
 *
 * Created: November 20, 2023
 * Modified: October 28, 2026
 * Authors: Oren Kohavi, Siddharta Laloux, Tanish Makadia, Yi Liu,
 *          Defne Doken, Aidan Wang, Ignacio Blancas Rodriguez, Alexander Thaep
 */

#include "main.h"

#include <string.h>

#include "SEGGER_RTT.h"
#include "globals.h"
#include "logging.h"
#include "mram_driver.h"
#include "watchdog_driver.h"

// The boot counter lives in the last 256-byte page of the 1 MB MRAM, clear of the development flash mirror
// (MRAM_FLASH_BASE_ADDRESS..+MRAM_FLASH_SIZE) and the MRAM hardware test region. mram_read_bytes() only reads whole
// pages, so the full page is read and the record is its first 8 bytes.
#define BOOT_COUNTER_MRAM_ADDRESS 0x000FFF00u
#define BOOT_COUNTER_PAGE_SIZE    256u
// Marks the record as written by this code, so never-written MRAM starts the count at zero instead of garbage
#define BOOT_COUNTER_MAGIC        0xB007C0DEu

typedef struct {
    uint32_t magic;
    uint32_t count;
} boot_counter_record_t;

static status_t PVDX_init(void) {
    // Segger Buffer 0 is pre-configured at compile time according to segger documentation
    // Config the logging output channel (assuming it's not zero)
    if (LOGGING_RTT_OUTPUT_CHANNEL != 0) {
        SEGGER_RTT_ConfigUpBuffer(LOGGING_RTT_OUTPUT_CHANNEL, "Log Output", SEGGER_RTT_LOG_BUFFER, SEGGER_RTT_LOG_BUFFER_SIZE,
                                  SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    }
    return SUCCESS;
}

// Busy-waits one second at a time (delay_ms is fine here: the scheduler never runs, so nothing else owns SysTick).
// The watchdog task never runs either, so the hardware watchdog is normally off. It is fed every second anyway in
// case it is still running from an earlier boot.
static void wait_seconds_feeding_watchdog(uint32_t seconds) {
    for (uint32_t s = 0; s < seconds; s++) {
        watchdog_feed(WDT);
        delay_ms(1000);
    }
}

int main(void) {
    /* ---------- HARDWARE & LOGGING INITIALIZATION + BOOTLOADER CHECK ---------- */

    /* Initializes MCU, drivers and middleware */
    atmel_start_init();
    PVDX_init();
    // info_impl(RTT_CTRL_RESET RTT_CTRL_CLEAR); // Reset the terminal
    info("--- Atmel & Hardware Initialization Complete ---\n");
    info("[+] Build Type: %s\n", BUILD_TYPE);
    info("[+] Build Date: %s\n", BUILD_DATE);
    info("[+] Build Time: %s\n", BUILD_TIME);
    info("[+] Built from branch: %s\n", GIT_BRANCH_NAME);
    info("[+] Built from commit: %s\n", GIT_COMMIT_HASH);

    /* ---------- BOOT COUNTER ---------- */

    mram_status_t mram_status = mram_init();
    for (uint8_t mram = 1; mram <= 3; mram++) {
        if (get_mram_state(mram_status, mram) != MRAM_STATE_OK) {
            warning_impl("[!] MRAM%u failed to initialize; the boot counter relies on the other modules\n", mram);
        }
    }

    // Read (majority-voted across the three MRAM modules), increment, and write back to all three
    static uint8_t page[BOOT_COUNTER_PAGE_SIZE];
    mram_read_bytes(BOOT_COUNTER_MRAM_ADDRESS, page, BOOT_COUNTER_PAGE_SIZE);
    boot_counter_record_t record;
    memcpy(&record, page, sizeof(record));
    if (record.magic != BOOT_COUNTER_MAGIC) {
        warning_impl("[!] No boot counter found in MRAM (magic: %x); starting from 0\n", record.magic);
        record.magic = BOOT_COUNTER_MAGIC;
        record.count = 0;
    }
    record.count++;
    mram_write_bytes(BOOT_COUNTER_MRAM_ADDRESS, (const uint8_t *)&record, sizeof(record));

    // Called outside info(): in RELEASE builds info() compiles out, but the boot counter should always be printed
    info_impl("[+] Boot counter: %lu\n", (unsigned long)record.count);

    /* ---------- IDLE FOREVER (scheduler is never started) ---------- */

    while (true) {
        wait_seconds_feeding_watchdog(1);
    }
}
