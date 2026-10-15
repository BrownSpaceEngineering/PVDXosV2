import sys
import argparse
import os

def extract_bin_data(input_path, start_hex, length_hex, output_path):
    try:
        start_addr = int(start_hex, 16)
        length = int(length_hex, 16)
    except ValueError:
        print("Error: Start address and length must be valid hex values (e.g., 0x1000).")
        sys.exit(1)

    if not os.path.isfile(input_path):
        print(f"Error: File '{input_path}' not found.")
        sys.exit(1)

    with open(input_path, 'rb') as f_in:
        f_in.seek(0, os.SEEK_END)
        file_size = f_in.tell()
        
        if start_addr >= file_size:
            print(f"Error: Start address {start_hex} is out of bounds for file of size {hex(file_size)}.")
            sys.exit(1)
            
        f_in.seek(start_addr)
        data = f_in.read(length)

    with open(output_path, 'wb') as f_out:
        f_out.write(data)

    if len(data) < length:
        print(f"Warning: Reached EOF. Extracted {hex(len(data))} bytes instead of requested {length_hex}.")
    else:
        print(f"Successfully extracted {hex(len(data))} bytes to {output_path}.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("input_file")
    parser.add_argument("start_address")
    parser.add_argument("length")
    parser.add_argument("output_file")
    args = parser.parse_args()

    extract_bin_data(args.input_file, args.start_address, args.length, args.output_file)