import math

import math

import image_format as fmt


def read_binary(path):
    with open(path, "rb") as binary:
        return binary.read()


def place_segment(flash, address, path, size):
    segment = read_binary(path)
    if len(segment) > size:
        raise ValueError(f"{path} is too large for its flash slot")
    flash[address:address + len(segment)] = segment


def place_checksummed_stage2(flash, address, path):
    segment = read_binary(path)
    if len(segment) >= fmt.FLASH_STAGE2_SIZE:
        raise ValueError(f"{path} leaves no room for its stage-2 checksum")
    flash[address:address + len(segment)] = segment
    checksum_address = address + fmt.FLASH_STAGE2_SIZE - 4
    checksum = fmt.crc32(flash[address:checksum_address])
    flash[checksum_address:checksum_address + 4] = checksum.to_bytes(4, "little")


flash = bytearray([0xFF] * fmt.FLASH_IMAGE_SIZE)
place_segment(flash, fmt.FLASH_STAGE1_ADDR, "bootloader/src/preboot.bin", fmt.FLASH_STAGE1_SIZE)
for index in range(fmt.FLASH_STAGE2_COUNT):
    address = fmt.FLASH_STAGE2_BASE + index * fmt.FLASH_STAGE2_SIZE
    path = f"bootloader/src/bootloader{index + 1}.bin"
    place_checksummed_stage2(flash, address, path)

app_binary = read_binary("src/PVDXos.bin")
app_size = math.ceil(len(app_binary) / fmt.IMAGE_BLOCK_SIZE) * fmt.IMAGE_BLOCK_SIZE
if not app_binary or app_size > fmt.FLASH_OS_APP_SIZE:
    raise ValueError("PVDXos.bin does not fit the 0x82000 executable slot")
flash[fmt.FLASH_OS_APP_ADDR:fmt.FLASH_OS_APP_ADDR + len(app_binary)] = app_binary

block_crcs = []
for block in range(app_size // fmt.IMAGE_BLOCK_SIZE):
    start = fmt.FLASH_OS_APP_ADDR + block * fmt.IMAGE_BLOCK_SIZE
    end = start + fmt.IMAGE_BLOCK_SIZE
    block_crcs.append(fmt.crc32(flash[start:end]))

header = fmt.pack_header(app_size, block_crcs)
if len(header) > fmt.FLASH_OS_HEADER_SIZE:
    raise ValueError("image header does not fit its OS block")
header_block = bytearray([0xFF] * fmt.FLASH_OS_HEADER_SIZE)
header_block[:len(header)] = header
flash[fmt.FLASH_OS_ADDR:fmt.FLASH_OS_APP_ADDR] = header_block

with open("flash.bin", "wb") as image:
    image.write(flash)

print("flash.bin created successfully!")