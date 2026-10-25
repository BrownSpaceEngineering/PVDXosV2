import os
import random


def flip_random_bits(filename, num_bits=5, chunks=False, target_padding=0, padding_size=3):

    bootloader_size_bytes = 0x2000

    """Flip random bits, option to flip one contiguous chunk, option to target area around padding ."""
    if num_bits < 0:
        raise ValueError("You actually have to flip some bits")
    if not isinstance(target_padding, (int, float)) or not 0 <= target_padding <= 1:
        raise ValueError("target_padding must be between 0 and 1.")
    if not isinstance(padding_size, int) or padding_size < 0:
        raise ValueError("padding_size must be a nonnegative integer number of bytes.")

    
    # Read file into a mutable bytearray
    with open(filename, "rb") as f:
        data = bytearray(f.read())

    file_size = len(data)
    total_bits = file_size * 8

    if total_bits < num_bits:
        raise ValueError("Number of flipped bits is greater than bits in file")
        
    #chunks
    if chunks:
        for bit_pos in range(random.randrange(total_bits - num_bits + 1), total_bits)[:num_bits]:
            data[bit_pos // 8] ^= 1 << (bit_pos % 8)
    
    # #flip bits
    # if mod is not None:
    #     for bit_pos in range(0, num_bits * mod, mod):
    #         data[bit_pos // 8] ^= 1 << (bit_pos % 8)


    #flip target_padding percent of bits within a certain range of target    
    #padding_bits in on each side
    if target_padding:
        padding_bits = padding_size*8
        targets = range(bootloader_size_bytes, min(len(data), 0x9000) + 1, bootloader_size_bytes)
        
        #for each byte, 
        for target_byte in targets:
            
            # if padding_size > bootloader_size_bytes - os.path.getsize(os.path.join(
            #         os.path.dirname(__file__), "..", "bootloader", f"bootloader{target_byte // bootloader_size_bytes}.bin")):
            #     raise ValueError("padding_size exceeds the bootloader's available padding.")

            
            #iterating through a random sample of bits within range
            for bit_pos in random.sample(range(-padding_bits, 0), int(padding_bits * target_padding)):
                data[target_byte + bit_pos // 8] ^= 1 << (bit_pos % 8)
    
    #flip random bits
    for bit_pos in random.sample(range(total_bits), num_bits):
        data[bit_pos // 8] ^= 1 << (bit_pos % 8)

    #flip 



    # Write modified data back
    with open(filename, "wb") as f:
        f.write(data)

    print(f"Flipped {num_bits} bits in {filename}.")


if __name__ == "__main__":
    flip_random_bits("flash.bin", 50, chunks=True, target_padding= 0.5)
