"""Flash and MRAM image constants shared with bootloader/src/image_format.h."""

import os
import re
import struct
import zlib

_HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bootloader", "src", "image_format.h")


def _parse_defines(path):
    defines = {}
    with open(path, encoding="ascii") as header:
        for line in header:
            match = re.match(r"#define (\w+) \((0x[0-9A-Fa-f]+|\d+)u\)", line)
            if match:
                defines[match.group(1)] = int(match.group(2), 0)
    return defines


_defines = _parse_defines(_HEADER)
IMAGE_BLOCK_SIZE = _defines["IMAGE_BLOCK_SIZE"]
FLASH_IMAGE_SIZE = _defines["FLASH_IMAGE_SIZE"]
FLASH_STAGE1_ADDR = _defines["FLASH_STAGE1_ADDR"]
FLASH_STAGE1_SIZE = _defines["FLASH_STAGE1_SIZE"]
FLASH_STAGE2_BASE = _defines["FLASH_STAGE2_BASE"]
FLASH_STAGE2_COUNT = _defines["FLASH_STAGE2_COUNT"]
FLASH_STAGE2_SIZE = _defines["FLASH_STAGE2_SIZE"]
FLASH_OS_ADDR = _defines["FLASH_OS_ADDR"]
FLASH_OS_SIZE = _defines["FLASH_OS_SIZE"]
FLASH_OS_HEADER_SIZE = IMAGE_BLOCK_SIZE
FLASH_OS_APP_ADDR = FLASH_OS_ADDR + FLASH_OS_HEADER_SIZE
FLASH_OS_APP_SIZE = FLASH_OS_SIZE - FLASH_OS_HEADER_SIZE
FLASH_OS_APP_BLOCK_COUNT = FLASH_OS_APP_SIZE // IMAGE_BLOCK_SIZE
IMAGE_MAGIC = _defines["IMAGE_MAGIC"]


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def pack_header(app_size, block_crcs):
    block_count = app_size // IMAGE_BLOCK_SIZE
    if app_size % IMAGE_BLOCK_SIZE or block_count != len(block_crcs) or block_count > FLASH_OS_APP_BLOCK_COUNT:
        raise ValueError("invalid app size or block CRC count")

    padded_crcs = list(block_crcs) + [0] * (FLASH_OS_APP_BLOCK_COUNT - block_count)
    fields = struct.pack(
        f"<III{FLASH_OS_APP_BLOCK_COUNT}I",
        IMAGE_MAGIC,
        app_size,
        block_count,
        *padded_crcs,
    )
    return fields + struct.pack("<I", crc32(fields))


IMAGE_HEADER_SIZE = struct.calcsize(f"<III{FLASH_OS_APP_BLOCK_COUNT}II")
if IMAGE_HEADER_SIZE != 268:
    raise RuntimeError("image header layout does not match image_format.h")
