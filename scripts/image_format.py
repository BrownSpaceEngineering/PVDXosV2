"""Flash/MRAM layout constants, read from bootloader/src/image_format.h so the scripts can't drift from the C code."""

import os
import re
import struct
import zlib

_HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bootloader", "src", "image_format.h")


def _parse_defines(path):
    defines = {}
    with open(path) as f:
        for line in f:
            match = re.match(r"#define (\w+) \((0x[0-9A-Fa-f]+|\d+)u\)", line)
            if match:
                defines[match.group(1)] = int(match.group(2), 0)
            # FLASH_STAGE2_ADDR(index) is a function-like macro: keep its base address
            match = re.match(r"#define FLASH_STAGE2_ADDR\(index\) \((0x[0-9A-Fa-f]+)u", line)
            if match:
                defines["FLASH_STAGE2_BASE"] = int(match.group(1), 16)
    return defines


_d = _parse_defines(_HEADER)

IMAGE_BLOCK_SIZE = _d["IMAGE_BLOCK_SIZE"]
FLASH_STAGE1_ADDR = _d["FLASH_STAGE1_ADDR"]
FLASH_STAGE1_SIZE = _d["FLASH_STAGE1_SIZE"]
FLASH_STAGE2_COUNT = _d["FLASH_STAGE2_COUNT"]
FLASH_STAGE2_SIZE = _d["FLASH_STAGE2_SIZE"]
FLASH_STAGE2_BASE = _d["FLASH_STAGE2_BASE"]
FLASH_METADATA_ADDR = _d["FLASH_METADATA_ADDR"]
FLASH_BOOT_REGION_SIZE = _d["FLASH_BOOT_REGION_SIZE"]
FLASH_APP_ADDR = _d["FLASH_APP_ADDR"]
MRAM_GOLDEN_APP_MAX_SIZE = _d["MRAM_GOLDEN_APP_MAX_SIZE"]
IMAGE_MAGIC = _d["IMAGE_MAGIC"]
IMAGE_FORMAT_VERSION = _d["IMAGE_FORMAT_VERSION"]

BOOT_REGION_BLOCKS = FLASH_BOOT_REGION_SIZE // IMAGE_BLOCK_SIZE
METADATA_BLOCK_INDEX = (FLASH_METADATA_ADDR - FLASH_STAGE1_ADDR) // IMAGE_BLOCK_SIZE
APP_MAX_BLOCKS = MRAM_GOLDEN_APP_MAX_SIZE // IMAGE_BLOCK_SIZE
CRC_SIZE = 4


def stage2_addr(index):
    return FLASH_STAGE2_BASE + index * FLASH_STAGE2_SIZE


def crc32(data):
    # Same CRC-32 (polynomial 0xEDB88320) as crc32() in the bootloader
    return zlib.crc32(bytes(data)) & 0xFFFFFFFF


def pack_header(app_size, boot_block_crc, app_block_crc):
    """Packs an image_header_t (all little-endian uint32_t fields, in declaration order)."""
    app_crcs = list(app_block_crc) + [0] * (APP_MAX_BLOCKS - len(app_block_crc))
    fields = struct.pack(
        f"<4I{BOOT_REGION_BLOCKS}I{APP_MAX_BLOCKS}I",
        IMAGE_MAGIC,
        IMAGE_FORMAT_VERSION,
        app_size,
        len(app_block_crc),
        *boot_block_crc,
        *app_crcs,
    )
    return fields + struct.pack("<I", crc32(fields))
