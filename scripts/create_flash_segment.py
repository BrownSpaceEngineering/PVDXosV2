import struct

bootloader_path1 = "bootloader/bootloader1.bin"
bootloader_path2 = "bootloader/bootloader2.bin"
bootloader_path3 = "bootloader/bootloader3.bin"
pvdxos_path = "src/PVDXos.bin"
flash_path = "flash.bin"

start_offset = 0x00010000

# PVDXos copies are aligned to the NVM erase block size (8 KB) so each copy can be erased independently
# - must match APP_FLASH_ALIGN in bootloader/bootloader.c
block_size = 0x2000

# the bootloader copies at most this many bytes to RAM
# - must match RAM_SIZE in bootloader/bootloader.c
ram_size = 0x3E000

# Define offsets
offsets = {
    "bootloader1": 0x00000000,
    "bootloader2": 0x00003000,
    "bootloader3": 0x00006000,
    # metadata gets its own 8 KB erase block so it can be rewritten without erasing a bootloader or PVDXos copy
    # - checksum addresses must match bootloader/startup.c
    "checksum1": 0x0000E000,
    "checksum2": 0x0000E004,
    "checksum3": 0x0000E008,
    # three copies of the PVDXos slot size (must match APP_FLASH_STEP_ADDR in bootloader/bootloader.c)
    "slot_size1": 0x0000E00C,
    "slot_size2": 0x0000E010,
    "slot_size3": 0x0000E014,
}


bootloaders = []
bootloader_sums = []
# Read binaries
with open(bootloader_path1, "rb") as f:
    bootloader = f.read()
bootloader_sum = sum(bootloader) % 256
bootloaders.append(bootloader)
bootloader_sums.append(bootloader_sum)

with open(bootloader_path2, "rb") as f:
    bootloader = f.read()
bootloader_sum = sum(bootloader) % 256
bootloaders.append(bootloader)
bootloader_sums.append(bootloader_sum)

with open(bootloader_path3, "rb") as f:
    bootloader = f.read()
bootloader_sum = sum(bootloader) % 256
bootloaders.append(bootloader)
bootloader_sums.append(bootloader_sum)

with open(pvdxos_path, "rb") as f:
    pvdxos = f.read()

# each PVDXos slot is the image size rounded up to a whole erase block
pvdxos_slot_size = (len(pvdxos) + block_size - 1) // block_size * block_size

# the bootloader rejects an empty slot or one larger than the app's RAM region
if pvdxos_slot_size == 0 or pvdxos_slot_size > ram_size:
    raise SystemExit(
        f"error: {pvdxos_path} is {len(pvdxos)} bytes ({pvdxos_slot_size} bytes block-aligned), but must be between 1 and {ram_size} bytes"
    )

offsets.update(
    {
        "pvdxos_1": start_offset,
        "pvdxos_2": start_offset + 1 * pvdxos_slot_size,
        "pvdxos_3": start_offset + 2 * pvdxos_slot_size,
    }
)

# Create flash image buffer (large enough to hold everything)
flash_size = offsets["pvdxos_3"] + pvdxos_slot_size
flash = bytearray([0x00] * flash_size)

# Place bootloader
# flash[offsets["bootloader"]:offsets["bootloader"] + len(bootloader)] = bootloader

for i in range(1, 4):
    start = offsets[f"bootloader{i}"]
    flash[start : start + len(bootloaders[i - 1])] = bootloaders[i - 1]
    sum_start = offsets[f"checksum{i}"]
    flash[sum_start] = bootloader_sums[i - 1]

# REMOVE THIS LINE LATER
# flash[offsets["bootloader1"]+49] = 0x00;
# flash[offsets["bootloader2"]+49] = 0x00;

# Place PVDXos slot sizes
for i in range(1, 4):
    start = offsets[f"slot_size{i}"]
    flash[start : start + 4] = struct.pack("<I", pvdxos_slot_size)

# Place PVDXos copies
for i in range(1, 4):
    start = offsets[f"pvdxos_{i}"]
    flash[start : start + len(pvdxos)] = pvdxos

# Write to flash.bin
with open(flash_path, "wb") as f:
    f.write(flash)

print(f"flash.bin created successfully! (PVDXos slot size {pvdxos_slot_size:#x})")
