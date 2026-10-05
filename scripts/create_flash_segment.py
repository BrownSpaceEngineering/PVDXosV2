bootloader_path1 = "bootloader/bootloader1.bin"
bootloader_path2 = "bootloader/bootloader2.bin"
bootloader_path3 = "bootloader/bootloader3.bin"
pvdxos_path = "src/PVDXos.bin"
flash_path = "flash.bin"

start_offset = 0x00010000

# space reserved for each PVDXos copy
# - must match APP_FLASH_STEP in bootloader/bootloader.c
# - it's at least as big as the app's RAM region (0x3E000), so anything the linker accepts fits
pvdxos_slot_size = 0x00020000

# Define offsets
offsets = {
    "bootloader1": 0x00000000,
    "bootloader2": 0x00003000,
    "bootloader3": 0x00006000,
    "checksum1": 0x00009000,
    "checksum2": 0x00009004,
    "checksum3": 0x00009008,
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

# PVDXos dynamic sizing

with open(pvdxos_path, "rb") as f:
    pvdxos = f.read()

pvdxos_size = len(pvdxos)
pvdxos_slot_size = pvdxos_slot_size + 0x10 #padding 

offsets.update({
    "pvdxos_1": start_offset,
    "pvdxos_2": start_offset + 1 * pvdxos_slot_size,
    "pvdxos_3": start_offset + 2 * pvdxos_slot_size,
})

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

# Place PVDXos copies
for i in range(1, 4):
    start = offsets[f"pvdxos_{i}"]
    flash[start : start + len(pvdxos)] = pvdxos

# Write to flash.bin
with open(flash_path, "wb") as f:
    f.write(flash)

print("flash.bin created successfully!")
