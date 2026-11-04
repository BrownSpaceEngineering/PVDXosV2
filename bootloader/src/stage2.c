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

#include "hal_cache.h"
#include "hri_nvmctrl_d51.h"
#include "image_format.h"

#define SCB_VTOR (*(volatile uint32_t *)0xE000ED08u)

// Must match BOOTLOADER_MAGIC_NUMBER_ADDRESS and BOOTLOADER_MAGIC_NUMBER_VALUE in src/main.h
#define BOOTLOADER_MAGIC_ADDR (0x47000000u)
#define BOOTLOADER_MAGIC_VALUE (0x50564458u) // ASCII 'PVDX'

#define REPAIR_ATTEMPTS 3
#define FLASH_LOCK_REGIONS 32 // NVMCTRL lock regions (NVMCTRL_REGIONS_NUM in hpl_nvmctrl.c)

static uint8_t block_buffer[IMAGE_BLOCK_SIZE] __attribute__((aligned(4)));
static uint8_t header_buffer[MRAM_GOLDEN_HEADER_READ_SIZE] __attribute__((aligned(4)));
static image_header_t golden_header;
static boot_report_t report;

/* ---------- FLASH HELPERS ---------- */

// Flash reads go through volatile pointers: block 0 lives at address 0, which C treats as a null pointer.
static void flash_copy(uint8_t *dst, uint32_t flash_addr, uint32_t size) {
    const volatile uint8_t *src = (const volatile uint8_t *)flash_addr;
    for (uint32_t i = 0; i < size; i++) {
        dst[i] = src[i];
    }
}

static bool flash_equals(uint32_t flash_addr, const uint8_t *data, uint32_t size) {
    const volatile uint8_t *flash = (const volatile uint8_t *)flash_addr;
    for (uint32_t i = 0; i < size; i++) {
        if (flash[i] != data[i]) {
            return false;
        }
    }
    return true;
}

static uint32_t flash_block_crc(uint32_t flash_addr) {
    flash_copy(block_buffer, flash_addr, IMAGE_BLOCK_SIZE);
    return crc32(block_buffer, IMAGE_BLOCK_SIZE);
}

// Erases one block and programs it with block_buffer, then reads it back. Retries up to REPAIR_ATTEMPTS times.
static bool flash_program_block(uint32_t flash_addr) {
    const uint32_t page_size = flash_get_page_size(&FLASH_0);
    const uint32_t region_size = page_size * flash_get_total_pages(&FLASH_0) / FLASH_LOCK_REGIONS;

    if (_flash_is_locked(&FLASH_0.dev, flash_addr)) {
        flash_unlock(&FLASH_0, flash_addr & ~(region_size - 1), region_size / page_size);
    }

    for (uint32_t attempt = 0; attempt < REPAIR_ATTEMPTS; attempt++) {
        if (flash_erase(&FLASH_0, flash_addr, IMAGE_BLOCK_SIZE / page_size) != ERR_NONE) {
            continue;
        }
        // flash_append programs the erased pages; flash_write would copy the whole block onto the stack again
        if (flash_append(&FLASH_0, flash_addr, block_buffer, IMAGE_BLOCK_SIZE) != ERR_NONE) {
            continue;
        }
        if (flash_equals(flash_addr, block_buffer, IMAGE_BLOCK_SIZE)) {
            return true;
        }
    }
    return false;
}

// Turns off the CMCC and NVM caches so that reads after a flash write see the new contents
static void disable_flash_caches(void) {
    cache_disable(CMCC);
    cache_invalidate_all(CMCC);
    hri_nvmctrl_set_CTRLA_CACHEDIS0_bit(NVMCTRL);
    hri_nvmctrl_set_CTRLA_CACHEDIS1_bit(NVMCTRL);
}

/* ---------- IMAGE HEADER AND BLOCKS ---------- */

static bool header_valid(const image_header_t *header) {
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

static uint32_t covered_block_count(const image_header_t *header) {
    return BOOT_REGION_BLOCKS + header->app_block_count;
}

static block_ref_t covered_block(const image_header_t *header, uint32_t n) {
    if (n < BOOT_REGION_BLOCKS) {
        return (block_ref_t){FLASH_STAGE1_ADDR + n * IMAGE_BLOCK_SIZE, MRAM_GOLDEN_BOOT_ADDR + n * IMAGE_BLOCK_SIZE,
                             header->boot_block_crc[n]};
    }
    n -= BOOT_REGION_BLOCKS;
    return (block_ref_t){FLASH_APP_ADDR + n * IMAGE_BLOCK_SIZE, MRAM_GOLDEN_APP_ADDR + n * IMAGE_BLOCK_SIZE,
                         header->app_block_crc[n]};
}

// Whether every block in flash (except the metadata block, which holds the header itself) matches `header`
static bool flash_matches(const image_header_t *header) {
    for (uint32_t n = 0; n < covered_block_count(header); n++) {
        if (n == METADATA_BLOCK_INDEX) {
            continue;
        }
        block_ref_t block = covered_block(header, n);
        if (flash_block_crc(block.flash_addr) != block.crc) {
            return false;
        }
    }
    return true;
}

/* ---------- GOLDEN COPY (MRAM) ---------- */

// Reads a golden block into block_buffer. Tries the majority vote first, then each chip alone, and accepts only
// data whose CRC matches: one dead chip must not take the golden copy down with it.
static bool golden_read_block(uint32_t mram_addr, uint32_t expected_crc) {
    if (mram_read_bytes(mram_addr, block_buffer, IMAGE_BLOCK_SIZE) && crc32(block_buffer, IMAGE_BLOCK_SIZE) == expected_crc) {
        return true;
    }
    for (uint8_t mram = 1; mram <= 3; mram++) {
        if (read_bytes(mram, mram_addr, block_buffer, IMAGE_BLOCK_SIZE) && crc32(block_buffer, IMAGE_BLOCK_SIZE) == expected_crc) {
            return true;
        }
    }
    return false;
}

// Reads the golden header into golden_header, the same way as golden_read_block
static bool golden_read_header(void) {
    if (mram_read_bytes(MRAM_GOLDEN_HEADER_ADDR, header_buffer, sizeof(header_buffer))) {
        memcpy(&golden_header, header_buffer, sizeof(golden_header));
        if (header_valid(&golden_header)) {
            return true;
        }
    }
    for (uint8_t mram = 1; mram <= 3; mram++) {
        if (read_bytes(mram, MRAM_GOLDEN_HEADER_ADDR, header_buffer, sizeof(header_buffer))) {
            memcpy(&golden_header, header_buffer, sizeof(golden_header));
            if (header_valid(&golden_header)) {
                return true;
            }
        }
    }
    return false;
}

// Copies flash into MRAM, unless flash doesn't match its own CRCs: damaged data must never become the golden copy. The
// old header is invalidated first and the new one written last, so a copy that is cut short leaves no header pointing
// at half-written data.
static bool write_golden(const image_header_t *flash_header) {
    if (!header_valid(flash_header) || !flash_matches(flash_header)) {
        return false;
    }

    memset(header_buffer, 0, sizeof(header_buffer));
    if (!mram_write_bytes(MRAM_GOLDEN_HEADER_ADDR, header_buffer, sizeof(header_buffer))) {
        return false;
    }

    for (uint32_t n = 0; n < covered_block_count(flash_header); n++) {
        if (n == METADATA_BLOCK_INDEX) {
            continue;
        }
        block_ref_t block = covered_block(flash_header, n);
        // Staged in RAM: the SPI driver treats a source address of 0 (block 0) as "no data"
        flash_copy(block_buffer, block.flash_addr, IMAGE_BLOCK_SIZE);
        if (!mram_write_bytes(block.mram_addr, block_buffer, IMAGE_BLOCK_SIZE) || !golden_read_block(block.mram_addr, block.crc)) {
            return false;
        }
    }

    memset(header_buffer, 0xFF, sizeof(header_buffer));
    memcpy(header_buffer, flash_header, sizeof(*flash_header));
    if (!mram_write_bytes(MRAM_GOLDEN_HEADER_ADDR, header_buffer, sizeof(header_buffer))) {
        return false;
    }
    return golden_read_header() && golden_header.header_crc == flash_header->header_crc;
}

/* ---------- REPAIR ---------- */

// Rewrites every flash block that does not match `reference` from the golden copy
static void repair_flash(const image_header_t *reference) {
    // BOOTPROT protects (15 - BOOTPROT) blocks from the start of flash; the running stage-2 copy is never touched
    const uint32_t protected_bytes = (15u - hri_nvmctrl_read_STATUS_BOOTPROT_bf(NVMCTRL)) * IMAGE_BLOCK_SIZE;
    const uint32_t own_slot = FLASH_STAGE2_ADDR(report.stage2_index);

    for (uint32_t n = 0; n < covered_block_count(reference); n++) {
        block_ref_t block = covered_block(reference, n);
        const bool is_metadata = (n == METADATA_BLOCK_INDEX);
        report.blocks_checked++;

        const bool okay = is_metadata ? flash_equals(block.flash_addr, (const uint8_t *)reference, sizeof(*reference))
                                      : flash_block_crc(block.flash_addr) == block.crc;
        if (okay) {
            continue;
        }

        if (block.flash_addr < protected_bytes || (block.flash_addr >= own_slot && block.flash_addr < own_slot + FLASH_STAGE2_SIZE)) {
            report.blocks_skipped++;
            continue;
        }

        bool have_data = true;
        if (is_metadata) {
            memset(block_buffer, 0xFF, sizeof(block_buffer));
            memcpy(block_buffer, reference, sizeof(*reference));
        } else {
            have_data = golden_read_block(block.mram_addr, block.crc);
        }

        if (have_data && flash_program_block(block.flash_addr)) {
            report.blocks_repaired++;
        } else {
            report.blocks_failed++;
        }
    }
}

/* ---------- JUMP TO PVDXOS ---------- */

static void go_to_app(void) {
    // Hand over the hardware the way PVDXos expects it after a reset: no bootloader peripherals or interrupts, NVM
    // caches back on
    spi_m_sync_disable(&SPI_MRAM);
    NVIC_DisableIRQ(NVMCTRL_0_IRQn);
    NVIC_DisableIRQ(NVMCTRL_1_IRQn);
    hri_nvmctrl_clear_CTRLA_CACHEDIS0_bit(NVMCTRL);
    hri_nvmctrl_clear_CTRLA_CACHEDIS1_bit(NVMCTRL);

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
    const mram_status_t mram_status = mram_init(); // Also runs atmel_start_init() (clocks, pins, SPI_MRAM, FLASH_0)
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
        const bool written = write_golden((const image_header_t *)FLASH_METADATA_ADDR);
        report.golden_state = written ? GOLDEN_WRITTEN : GOLDEN_WRITE_FAILED;
    } else if (golden_read_header()) {
        report.golden_state = GOLDEN_VALID;
        repair_flash(&golden_header);
    } else {
        report.golden_state = GOLDEN_UNREADABLE;
    }

    go_to_app();
    __builtin_unreachable();
}
