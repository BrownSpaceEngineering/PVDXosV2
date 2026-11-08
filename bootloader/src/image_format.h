#ifndef IMAGE_FORMAT_H
#define IMAGE_FORMAT_H

#include <stdint.h>

#define IMAGE_BLOCK_SIZE (0x00002000u)
#define FLASH_IMAGE_SIZE (0x00100000u)
#define FLASH_STAGE1_ADDR (0x00000000u)
#define FLASH_STAGE1_SIZE (0x00002000u)
#define FLASH_STAGE2_BASE (0x00002000u)
#define FLASH_STAGE2_COUNT (8u)
#define FLASH_STAGE2_SIZE (0x00002000u)
#define FLASH_OS_ADDR (0x00080000u)
#define FLASH_OS_SIZE (0x00080000u)
#define FLASH_OS_HEADER_SIZE IMAGE_BLOCK_SIZE
#define IMAGE_HEADER_READ_SIZE (0x00000200u)
#define FLASH_OS_APP_ADDR (FLASH_OS_ADDR + FLASH_OS_HEADER_SIZE)
#define FLASH_OS_APP_SIZE (FLASH_OS_SIZE - FLASH_OS_HEADER_SIZE)
#define FLASH_OS_APP_BLOCK_COUNT (FLASH_OS_APP_SIZE / IMAGE_BLOCK_SIZE)
#define MRAM_OS_ADDR (0x00000000u)
#define MRAM_OS_SIZE (FLASH_OS_SIZE)
#define MRAM_BOOTLOADER_ADDR (0x00080000u)
#define MRAM_BOOTLOADER_SIZE (0x00012000u)

#define IMAGE_MAGIC (0x50564458u)

typedef struct {
	uint32_t magic;
	uint32_t app_size;       // Executable size rounded up to IMAGE_BLOCK_SIZE.
	uint32_t app_block_count;
	uint32_t app_block_crc[FLASH_OS_APP_BLOCK_COUNT];
	uint32_t header_crc;     // CRC-32 of the preceding header fields.
} image_header_t;

_Static_assert(FLASH_STAGE2_BASE + FLASH_STAGE2_COUNT * FLASH_STAGE2_SIZE <= 0x00012000u, "stage-2 copies exceed reserved flash");
_Static_assert(FLASH_OS_SIZE % IMAGE_BLOCK_SIZE == 0u, "OS capacity must contain whole erase rows");
_Static_assert(FLASH_OS_ADDR + FLASH_OS_SIZE == FLASH_IMAGE_SIZE, "OS must fill the second flash bank");
_Static_assert(FLASH_OS_HEADER_SIZE == 0x00002000u, "OS header must occupy one erase row");
_Static_assert(sizeof(image_header_t) == 268u, "image header format changed");

#endif
