/**
 * stage2.c
 *
 * Stage 2 of the PVDXos bootloader (stage 1 at 0x0 jumps here). Checks every flash block that makes up this
 * build against the golden copy in MRAM, rewrites any block that is wrong, and then jumps to PVDXos, which runs
 * from flash at FLASH_APP_ADDR. The flash and MRAM layouts are in image_format.h.
 *
 * When a debugger has left WRITE_TO_MRAM_REQUEST_MAGIC in backup RAM (flash_to_mram.jlink), it copies flash into
 * MRAM on that one boot. Otherwise it copies MRAM into any flash block that doesn't match it, and never writes to
 * MRAM. The outcome is left in backup RAM (boot_report_t) for PVDXos to log.
 *
 * Data is never written to flash unless its CRC matches the reference header.
 */

#include <stddef.h>
#include <string.h>

#include "mram_driver.h" // atmel_start.h first: the ASF headers below need it

#include "flash_driver.h"
#include "image_format.h"

#define SCB_VTOR (*(volatile uint32_t *)0xE000ED08u)

// Must match BOOTLOADER_MAGIC_NUMBER_ADDRESS and BOOTLOADER_MAGIC_NUMBER_VALUE in src/main.h
#define BOOTLOADER_MAGIC_ADDR (0x47000000u)
#define BOOTLOADER_MAGIC_VALUE (0x50564458u) // ASCII 'PVDX'

static uint8_t block_buffer[IMAGE_BLOCK_SIZE] __attribute__((aligned(4)));
static uint8_t header_buffer[MRAM_GOLDEN_HEADER_READ_SIZE] __attribute__((aligned(4)));
static image_header_t golden_header;
static boot_report_t report;

/* ---------- IMAGE HEADER AND BLOCKS ---------- */

static bool is_header_valid(const image_header_t *header) {
    return header->magic == IMAGE_MAGIC && header->format_version == IMAGE_FORMAT_VERSION &&
           header->app_block_count <= APP_MAX_BLOCKS && header->app_size == header->app_block_count * IMAGE_BLOCK_SIZE &&
           crc32((const uint8_t *)header, offsetof(image_header_t, header_crc)) == header->header_crc;
}

// The covered blocks are the boot region (stage 1, stage-2 copies, metadata) followed by the PVDXos blocks
typedef struct {
    uint32_t flash_addr;
    uint32_t mram_addr;
    uint32_t crc;
} block_ref_t;

static uint32_t count_covered_blocks(const image_header_t *header) {
    return BOOT_REGION_BLOCKS + header->app_block_count;
}

static block_ref_t get_covered_block(const image_header_t *header, uint32_t n) {
    if (n < BOOT_REGION_BLOCKS) {
        return (block_ref_t){FLASH_STAGE1_ADDR + n * IMAGE_BLOCK_SIZE, MRAM_GOLDEN_BOOT_ADDR + n * IMAGE_BLOCK_SIZE,
                             header->boot_block_crc[n]};
    }
    n -= BOOT_REGION_BLOCKS;
    return (block_ref_t){FLASH_APP_ADDR + n * IMAGE_BLOCK_SIZE, MRAM_GOLDEN_APP_ADDR + n * IMAGE_BLOCK_SIZE,
                         header->app_block_crc[n]};
}

/* ---------- GOLDEN COPY (MRAM) ---------- */

// Reads a golden block into block_buffer and checks it against `expected_crc`
static bool read_golden_block(uint32_t mram_addr, uint32_t expected_crc) {
    return mram_read_bytes(mram_addr, block_buffer, IMAGE_BLOCK_SIZE) && crc32(block_buffer, IMAGE_BLOCK_SIZE) == expected_crc;
}

// Reads the golden header into golden_header and checks that it is valid
static bool read_golden_header(void) {
    return mram_read_bytes(MRAM_GOLDEN_HEADER_ADDR, (uint8_t *)&golden_header, sizeof(golden_header)) &&
           is_header_valid(&golden_header);
}

// Copies flash into MRAM as it is, even if it doesn't match its own CRCs: the golden header is rebuilt from the data
// actually copied. The block count comes from the flash header, or is the whole golden-copy slot if that header is
// invalid. The old header is invalidated first and the new one written last, so a copy that is cut short leaves no
// header pointing at half-written data.
static bool write_golden_copy(const image_header_t *flash_header) {
    static image_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = IMAGE_MAGIC;
    header.format_version = IMAGE_FORMAT_VERSION;
    header.app_block_count = is_header_valid(flash_header) ? flash_header->app_block_count : APP_MAX_BLOCKS;
    header.app_size = header.app_block_count * IMAGE_BLOCK_SIZE;

    memset(header_buffer, 0, sizeof(header_buffer));
    if (!mram_write_bytes(MRAM_GOLDEN_HEADER_ADDR, header_buffer, sizeof(header_buffer))) {
        return false;
    }

    for (uint32_t n = 0; n < count_covered_blocks(&header); n++) {
        if (n == METADATA_BLOCK_INDEX) {
            continue;
        }
        block_ref_t block = get_covered_block(&header, n);
        // Staged in RAM: the SPI driver treats a source address of 0 (block 0) as "no data"
        memcpy(block_buffer, (const void *)block.flash_addr, IMAGE_BLOCK_SIZE);
        const uint32_t crc = crc32(block_buffer, IMAGE_BLOCK_SIZE);
        if (n < BOOT_REGION_BLOCKS) {
            header.boot_block_crc[n] = crc;
        } else {
            header.app_block_crc[n - BOOT_REGION_BLOCKS] = crc;
        }
        if (!mram_write_bytes(block.mram_addr, block_buffer, IMAGE_BLOCK_SIZE) || !read_golden_block(block.mram_addr, crc)) {
            return false;
        }
    }
    header.header_crc = crc32((const uint8_t *)&header, offsetof(image_header_t, header_crc));

    memset(header_buffer, 0xFF, sizeof(header_buffer));
    memcpy(header_buffer, &header, sizeof(header));
    if (!mram_write_bytes(MRAM_GOLDEN_HEADER_ADDR, header_buffer, sizeof(header_buffer))) {
        return false;
    }
    return read_golden_header() && golden_header.header_crc == header.header_crc;
}

/* ---------- REPAIR ---------- */

// Rewrites every PVDXos flash block that does not match `reference` from the golden copy. The bootloader's own blocks
// (stage 1, the stage-2 copies and the metadata) are never checked or rewritten.
static void repair_flash(const image_header_t *reference) {
    for (uint32_t n = BOOT_REGION_BLOCKS; n < count_covered_blocks(reference); n++) {
        block_ref_t block = get_covered_block(reference, n);
        report.blocks_checked++;

        if (crc32((const uint8_t *)block.flash_addr, IMAGE_BLOCK_SIZE) == block.crc) {
            continue;
        }

        if (read_golden_block(block.mram_addr, block.crc) && program_flash_block(block.flash_addr, block_buffer, IMAGE_BLOCK_SIZE)) {
            report.blocks_repaired++;
        } else {
            report.blocks_failed++;
        }
    }
}

/* ---------- JUMP TO PVDXOS ---------- */

static void go_to_app(void) {
    // Hand over the hardware the way PVDXos expects it after a reset: no bootloader peripherals or interrupts, flash
    // caches back on
    spi_m_sync_disable(&SPI_MRAM);
    NVIC_DisableIRQ(NVMCTRL_0_IRQn);
    NVIC_DisableIRQ(NVMCTRL_1_IRQn);
    enable_flash_caches();

    *(volatile uint32_t *)BOOTLOADER_MAGIC_ADDR = BOOTLOADER_MAGIC_VALUE;
    memcpy((void *)BOOT_REPORT_ADDR, &report, sizeof(report));

    // Read app's vector table (first value is SP, second value is PC)
    const uint32_t *vector_table = (const uint32_t *)FLASH_APP_ADDR;
    uint32_t desired_sp = vector_table[0];
    uint32_t desired_pc = vector_table[1] | 0x1; // Thumb mode

    SCB_VTOR = FLASH_APP_ADDR;
    __asm__ volatile(
        "mov sp, %0\n" // Move the value in desired_sp into SP
        "bx %1"        // Branch to the address contained in desired_pc
        :
        : "r"(desired_sp), "r"(desired_pc)
        :);
    __builtin_unreachable();
}

int main(void) {
    atmel_start_init();
    spi_m_sync_enable(&SPI_MRAM);

    const mram_status_t mram_status = mram_init();
    disable_flash_caches();

    report = (boot_report_t){
        .magic = BOOT_REPORT_MAGIC,
        .mram_status = mram_status,
        .stage2_index = (uint8_t)((SCB_VTOR - FLASH_STAGE2_ADDR(0)) / FLASH_STAGE2_SIZE),
    };

    // Cleared before copying, so the request is used once even if this boot is cut short
    volatile uint32_t *const request = (volatile uint32_t *)WRITE_TO_MRAM_REQUEST_ADDR;
    const bool write_flash_to_mram = (*request == WRITE_TO_MRAM_REQUEST_MAGIC);
    *request = 0;

    if (write_flash_to_mram) {
        const bool written = write_golden_copy((const image_header_t *)FLASH_METADATA_ADDR);
        report.golden_state = written ? GOLDEN_WRITTEN : GOLDEN_WRITE_FAILED;
    } else if (read_golden_header()) {
        report.golden_state = GOLDEN_VALID;
        repair_flash(&golden_header);
    } else {
        report.golden_state = GOLDEN_UNREADABLE;
    }

    go_to_app();
    __builtin_unreachable();
}
