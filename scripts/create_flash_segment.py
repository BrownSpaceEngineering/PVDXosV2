bootloader_path1 = "bootloader/bootloader1.bin"
bootloader_path2 = "bootloader/bootloader2.bin"
bootloader_path3 = "bootloader/bootloader3.bin"
pvdxos_path = "src/PVDXos.bin"
flash_path = "flash.bin"

# Define offsets
offsets = {
    "bootloader1": 0x00000000,
    "bootloader2": 0x00003000,
    "bootloader3": 0x00006000,
    "checksums": 0x00009000,
    "pvdxos_1": 0x00010000,
    "pvdxos_2": 0x00020000,
    "pvdxos_3": 0x00030000,
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

# Create flash image buffer (large enough to hold everything)
flash_size = 0x00040000  # adjust if needed
flash = bytearray([0x00] * flash_size) 

# Place bootloader
# flash[offsets["bootloader"]:offsets["bootloader"] + len(bootloader)] = bootloader

for i in range(1, 4):
    start = offsets[f"bootloader{i}"]
    flash[start:start + len(bootloaders[i-1])] = bootloaders[i-1]
    flash[start+len(bootloaders[i-1])] = bootloader_sums[i-1]

# Place PVDXos copies
for i in range(1, 4):
    start = offsets[f"pvdxos_{i}"]
    flash[start:start + len(pvdxos)] = pvdxos

# Write to flash.bin
with open(flash_path, "wb") as f:
    f.write(flash)

print("flash.bin created successfully!")
