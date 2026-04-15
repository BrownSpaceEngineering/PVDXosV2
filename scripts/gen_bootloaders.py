import os, sys

BOOTLOADER_SIZE = 0x3000

bootloader_count = int(sys.argv[1])

def write_linker_files():
    with open("../bootloader/src/bootloader_flash.ld.template", "r") as f:
        template = f.read()
    
    for i in range(1, bootloader_count + 1):
        linker_i = template.replace("$BL_START", f"0x{((i - 1) * BOOTLOADER_SIZE):08X}")
        with open(f"../bootloader/src/bootloader_flash{i}.ld", "w") as f:
            f.write(linker_i)

def write_index_file():
    h_file = """
#if defined(BOOTLOADER_1)
#define BOOTLOADER_INDEX 0"""
    for i in range(2, bootloader_count + 1):
        h_file += f"""
#elif defined(BOOTLOADER_{i})
#define BOOTLOADER_INDEX {i - 1}"""
    h_file += """
#else
#define BOOTLOADER_INDEX 255
#endif"""
    
    with open("../bootloader/src/bootloader_index.h", "w") as f:
        f.write(h_file)

os.chdir(os.path.dirname(os.path.abspath(__file__)))

write_linker_files()
write_index_file()