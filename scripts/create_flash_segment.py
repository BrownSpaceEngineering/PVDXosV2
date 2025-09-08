bootloader_path = "bootloader/bootloader.bin"
pvdxos_path = "src/PVDXos.bin"
flash_path = "flash.bin"

# Define offsets
offsets = {
    "bootloader": 0x00000000,
    "pvdxos_1": 0x00010000,
    "pvdxos_2": 0x00020000,
    "pvdxos_3": 0x00030000,
}

# Read binaries
with open(bootloader_path, "rb") as f:
    bootloader = f.read()

with open(pvdxos_path, "rb") as f:
    pvdxos = f.read()

# Create flash image buffer (large enough to hold everything)
flash_size = 0x00040000  # adjust if needed
flash = bytearray([0x00] * flash_size) 

# Place bootloader
flash[offsets["bootloader"]:offsets["bootloader"] + len(bootloader)] = bootloader

# Place PVDXos copies
for i in range(1, 4):
    start = offsets[f"pvdxos_{i}"]
    flash[start:start + len(pvdxos)] = pvdxos

# Write to flash.bin
with open(flash_path, "wb") as f:
    f.write(flash)

print("flash.bin created successfully!")
