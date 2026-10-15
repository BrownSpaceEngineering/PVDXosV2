import os, sys

BOOTLOADER_SIZE = 0x2000

bootloader_count = int(sys.argv[1])

def write_linker_files():
    with open("../bootloader/src/bootloader_flash.ld.template", "r") as f:
        template = f.read()

    preboot_linker = template.replace("$BL_START", "0x00000000")
    with open("../bootloader/src/preboot_flash.ld", "w") as f:
        f.write(preboot_linker)
    
    for i in range(1, bootloader_count + 1):
        linker_i = template.replace("$BL_START", f"0x{(i * BOOTLOADER_SIZE):08X}")
        with open(f"../bootloader/src/bootloader_flash{i}.ld", "w") as f:
            f.write(linker_i)

os.chdir(os.path.dirname(os.path.abspath(__file__)))

write_linker_files()