import argparse
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import image_format as fmt  # noqa: E402

# Byte ranges of flash.bin that --region can target (see bootloader/src/image_format.h)
REGIONS = {
    "boot": lambda size: (fmt.FLASH_STAGE1_ADDR, fmt.FLASH_BOOT_REGION_SIZE),  # stage 1, stage-2 copies, metadata
    "app": lambda size: (fmt.FLASH_APP_ADDR, size),                          # PVDXos
    "all": lambda size: (0, size),
}


def flip_random_bits(filename, num_bits=5, region="all"):
    # Read file into a mutable bytearray
    with open(filename, "rb") as f:
        data = bytearray(f.read())

    start, end = REGIONS[region](len(data))
    total_bits = (end - start) * 8

    if total_bits < num_bits:
        raise ValueError("Region is too small to flip the requested number of bits.")

    # Choose unique bit positions
    bit_positions = random.sample(range(start * 8, end * 8), num_bits)

    for bit_pos in bit_positions:
        byte_index = bit_pos // 8
        bit_index = bit_pos % 8
        data[byte_index] ^= (1 << bit_index)  # flip the bit

    # Write modified data back
    with open(filename, "wb") as f:
        f.write(data)

    print(f"Flipped {num_bits} random bits in the {region} region of {filename}.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Flip random bits in flash.bin to exercise the bootloader's repair path")
    parser.add_argument("--region", choices=REGIONS.keys(), default="all")
    parser.add_argument("--bits", type=int, default=50)
    args = parser.parse_args()
    flip_random_bits("flash.bin", args.bits, args.region)
