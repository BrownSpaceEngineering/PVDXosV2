import math

BOOTLOADER_COUNT = 8
BOOTLOADER_SIZE = 0x2000
FLASH_BIN_SIZE = 0x80000
REFLASH_BLOCK_SIZE = 512 # bytes
REFLASH_BLOCK_COUNT = math.floor(FLASH_BIN_SIZE / REFLASH_BLOCK_SIZE)

def crc32(data: bytearray) -> int:
    table = []
    for i in range(256):
        crc = i
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
        table.append(crc)

    crc = 0xFFFFFFFF
    for byte in data:
        crc = (crc >> 8) ^ table[(crc ^ byte) & 0xFF]
    crc = crc ^ 0xFFFFFFFF

    return (crc & 0xFFFFFFFF).to_bytes(4, "little")

def write_checksummed_segment(flash: bytearray, offset: int, path: str, size: int):
    with open(path, "rb") as f:
        segment = f.read()

    if len(segment) >= size:
        raise Exception("Segment binary too large")
    
    flash[offset:offset + len(segment)] = segment

    checksum = crc32(flash[offset:offset + size - 4])
    flash[offset + size - 4:offset + size] = checksum

def write_normal_segment(flash: bytearray, offset: int, path: str, size: int):
    with open(path, "rb") as f:
        segment = f.read()

    if len(segment) >= size:
        raise Exception("Segment binary too large")
    
    flash[offset:offset + len(segment)] = segment

def write_bytes(flash: bytearray, offset: int, segment: int, size: int):
    if len(segment) >= size:
        raise Exception("Segment too larger")
    
    flash[offset:offset + len(segment)] = segment

flash = bytearray([0xFF] * FLASH_BIN_SIZE)

write_normal_segment(flash, 0, "bootloader/src/preboot.bin", BOOTLOADER_SIZE)
for i in range(BOOTLOADER_COUNT):
    write_checksummed_segment(flash, (i + 1) * BOOTLOADER_SIZE, f"bootloader/src/bootloader{i + 1}.bin", BOOTLOADER_SIZE)
write_checksummed_segment(flash, 0x20000, "src/PVDXos.bin", 0x20000)
write_checksummed_segment(flash, 0x40000, "src/PVDXos.bin", 0x20000)
write_checksummed_segment(flash, 0x60000, "src/PVDXos.bin", 0x20000)
for i in range(REFLASH_BLOCK_COUNT):
    write_bytes(flash, FLASH_BIN_SIZE + i * REFLASH_BLOCK_SIZE,
        crc32(flash[(i * REFLASH_BLOCK_SIZE):((i + 1) * REFLASH_BLOCK_SIZE)]), REFLASH_BLOCK_SIZE)

with open("flash.bin", "wb") as f:
    f.write(flash)

print("flash.bin created successfully!")