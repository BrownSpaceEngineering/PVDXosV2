import os, sys

import image_format as fmt

# Generates the stage 1 and stage 2 linker scripts from bootloader/src/bootloader_flash.ld.template, using the flash
# layout in bootloader/src/image_format.h. Each stage-2 copy leaves its last 4 bytes free for its CRC32.

stage2_count = int(sys.argv[1])
if stage2_count != fmt.FLASH_STAGE2_COUNT:
    raise SystemExit(f"error: NUM_STAGE2_COPIES is {stage2_count}, but image_format.h has {fmt.FLASH_STAGE2_COUNT} stage-2 copies")

def write_linker_files():
    with open("../bootloader/src/bootloader_flash.ld.template", "r") as f:
        template = f.read()

    stage1_linker = template.replace("$BL_START", f"0x{fmt.FLASH_STAGE1_ADDR:08X}").replace("$BL_LENGTH", f"0x{fmt.FLASH_STAGE1_SIZE:08X}")
    with open("../bootloader/src/stage1_flash.ld", "w") as f:
        f.write(stage1_linker)

    for i in range(1, stage2_count + 1):
        start = fmt.stage2_addr(i - 1)
        length = fmt.FLASH_STAGE2_SIZE - fmt.CRC_SIZE
        linker_i = template.replace("$BL_START", f"0x{start:08X}").replace("$BL_LENGTH", f"0x{length:08X}")
        with open(f"../bootloader/src/stage2_flash{i}.ld", "w") as f:
            f.write(linker_i)

os.chdir(os.path.dirname(os.path.abspath(__file__)))

write_linker_files()
