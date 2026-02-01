def write_checksummed_segment(flash: bytearray, offset: int, path: str, size: int):
    with open(path, "rb") as f:
        segment = f.read()

    if len(segment) >= size:
        raise Exception("Segment binary too large")
    
    flash[offset:offset + size] = segment

    checksum = (256 - (sum(segment) % 256)) % 256
    flash[offset + size] = checksum

def write_normal_segment(flash: bytearray, offset: int, path: str, size: int):
    with open(path, "rb") as f:
        segment = f.read()

    if len(segment) >= size:
        raise Exception("Segment binary too large")
    
    flash[offset:offset + size] = segment

flash = bytearray([0x00] * 0x40000)

write_checksummed_segment(flash, 0x00000, "bootloader/src/bootloader1.bin", 0x3000)
write_checksummed_segment(flash, 0x03000, "bootloader/src/bootloader2.bin", 0x3000)
write_checksummed_segment(flash, 0x06000, "bootloader/src/bootloader3.bin", 0x3000)

write_normal_segment(flash, 0x10000, "src/PVDXos.bin", 0x10000)
write_normal_segment(flash, 0x20000, "src/PVDXos.bin", 0x10000)
write_normal_segment(flash, 0x30000, "src/PVDXos.bin", 0x10000)

with open("flash.bin", "wb") as f:
    f.write(flash)

print("flash.bin created successfully!")