"""Assembles flash.bin: the stage 1 bootloader, three CRC-checked stage 2 bootloaders, the image metadata block and PVDXos.

Layout (see bootloader/src/image_format.h):
    0x00000  stage 1 bootloader
    0x02000  stage-2 bootloader copies (16 KB each, CRC32 in the last 4 bytes)
    0x0E000  metadata: image_header_t with a CRC32 for every block, used by the bootloader to check flash
    0x10000  PVDXos (runs in place from flash), padded with 0xFF to a whole number of 8 KB blocks
Gaps are 0xFF, the value of erased flash.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import image_format as fmt  # noqa: E402

stage1_path = "bootloader/src/stage1.bin"
stage2_paths = [f"bootloader/src/stage2_{i + 1}.bin" for i in range(fmt.FLASH_STAGE2_COUNT)]
pvdxos_path = "src/PVDXos.bin"
flash_path = "flash.bin"


def read(path):
    with open(path, "rb") as f:
        return f.read()


def place(flash, offset, data, limit, name):
    if len(data) > limit:
        raise SystemExit(f"error: {name} is {len(data)} bytes, but its slot holds {limit} bytes")
    flash[offset : offset + len(data)] = data


pvdxos = read(pvdxos_path)
app_blocks = (len(pvdxos) + fmt.IMAGE_BLOCK_SIZE - 1) // fmt.IMAGE_BLOCK_SIZE
app_size = app_blocks * fmt.IMAGE_BLOCK_SIZE
if app_blocks == 0 or app_blocks > fmt.APP_MAX_BLOCKS:
    raise SystemExit(f"error: {pvdxos_path} is {len(pvdxos)} bytes; it must be 1 to {fmt.MRAM_GOLDEN_APP_MAX_SIZE} bytes")

flash = bytearray([0xFF] * (fmt.FLASH_APP_ADDR + app_size))

place(flash, fmt.FLASH_STAGE1_ADDR, read(stage1_path), fmt.FLASH_STAGE1_SIZE, stage1_path)

for i, path in enumerate(stage2_paths):
    start = fmt.stage2_addr(i)
    body_size = fmt.FLASH_STAGE2_SIZE - fmt.CRC_SIZE
    place(flash, start, read(path), body_size, path)
    flash[start + body_size : start + fmt.FLASH_STAGE2_SIZE] = fmt.crc32(flash[start : start + body_size]).to_bytes(4, "little")

place(flash, fmt.FLASH_APP_ADDR, pvdxos, app_size, pvdxos_path)


def block(offset):
    return flash[offset : offset + fmt.IMAGE_BLOCK_SIZE]


# CRC32 of every block the bootloader checks; the metadata block can't contain its own CRC, so its entry is 0
boot_block_crc = [
    0 if n == fmt.METADATA_BLOCK_INDEX else fmt.crc32(block(fmt.FLASH_STAGE1_ADDR + n * fmt.IMAGE_BLOCK_SIZE))
    for n in range(fmt.BOOT_REGION_BLOCKS)
]
app_block_crc = [fmt.crc32(block(fmt.FLASH_APP_ADDR + n * fmt.IMAGE_BLOCK_SIZE)) for n in range(app_blocks)]

header = fmt.pack_header(app_size, boot_block_crc, app_block_crc)
place(flash, fmt.FLASH_METADATA_ADDR, header, fmt.IMAGE_BLOCK_SIZE, "image header")

with open(flash_path, "wb") as f:
    f.write(flash)

print(f"flash.bin created successfully! (PVDXos {len(pvdxos)} bytes in {app_blocks} blocks)")
