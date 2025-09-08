import os
import random

def flip_random_bits(filename, num_bits=5):
    # Read file into a mutable bytearray
    with open(filename, "rb") as f:
        data = bytearray(f.read())

    file_size = len(data)
    total_bits = file_size * 8

    if total_bits < num_bits:
        raise ValueError("File is too small to flip the requested number of bits.")

    # Choose unique bit positions
    bit_positions = random.sample(range(total_bits), num_bits)

    for bit_pos in bit_positions:
        byte_index = bit_pos // 8
        bit_index = bit_pos % 8
        data[byte_index] ^= (1 << bit_index)  # flip the bit

    # Write modified data back
    with open(filename, "wb") as f:
        f.write(data)

    print(f"Flipped {num_bits} random bits in {filename}.")


if __name__ == "__main__":
    flip_random_bits("flash.bin", 50)
