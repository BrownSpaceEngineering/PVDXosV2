from PIL import Image
import numpy as np

# SSD1362 display resolution
screen_w = 256
screen_h = 64


def image_to_buffer(image_path):
    """Converts the inputted image to a buffer where each byte represents two horizontally consecutive pixels (4-bit grayscale)."""
    # Open the image and convert it to greyscale using luma conversion
    img = Image.open(image_path).convert("L")

    # Resize the image to 256x64 pixels
    img = img.resize((screen_w, screen_h))

    # Convert the image to a numpy array
    pixels = np.array(img)

    # Normalize pixel values to 0-15 (4-bit grayscale)
    pixels = (pixels // 16).astype(np.uint8)

    # Copy the pixels to a buffer, combining two horizontally consecutive 4-bit pixels into one byte
    buffer = np.zeros((screen_h, screen_w//2), dtype=np.uint8)

    for y in range(screen_h):
        for x in range(0, screen_w, 2):
            buffer[y, x//2] = (pixels[y, x] << 4) | pixels[y, x+1]

    return buffer


def export_buffer(buffer, image_name):
    """Writes the inputted buffer to `./src/tasks/display/image_buffers/` as a C array."""
    # convert the buffer to a hexadecimal string of the form: "0xAB, 0xCD, 0xEF, ..."
    hex_string = buffer.tobytes().hex()
    formatted_hex_string = ", ".join([f"0x{hex_string[i:i+2]}" for i in range(0, len(hex_string), 2)])

    # Write the buffer to a C array in a header file (double curly braces are used to escape the format string)
    c_include_guard_start_string = f"#ifndef IMAGE_BUFFER_{image_name.upper()}_H\n#define IMAGE_BUFFER_{image_name.upper()}_H"
    c_array_string = f"const uint8_t IMAGE_BUFFER_{image_name.upper()}[] = {{ {formatted_hex_string} }};" 
    c_include_guard_end_string = f"#endif // IMAGE_BUFFER_{image_name.upper()}_H"
    c_header_string = f"{c_include_guard_start_string}\n\n{c_array_string}\n\n{c_include_guard_end_string}"

    with open(f"./src/tasks/display/image_buffers/image_buffer_{image_name}.h", "w") as file:
        file.write(c_header_string)


def unpack_buffer(buffer):
    """Unpacks the inputted buffer (with 2 pixels per byte) into a 2D array of individual pixels. 
    Use this function to verify the correctness of the buffer."""
    height, width = buffer.shape
    unpacked = np.zeros((height, width * 2), dtype=np.uint8)

    for y in range(height):
        for x in range(width):
            unpacked[y, x*2] = buffer[y, x] >> 4   # High nibble for the left pixel
            unpacked[y, x*2 + 1] = buffer[y, x] & 0x0F  # Low nibble for the right pixel

    return unpacked


def visualize_buffer(buffer):
    """Visualizes the inputted buffer as a temporary image. Use this function to verify the correctness of the buffer."""
    unpacked = unpack_buffer(buffer)
    unpacked *= 17  # Scale up 0-15 range to 0-255 range
    image = Image.fromarray(unpacked, 'L')
    image.show()


# Example usage (a C header file will be created in the `./src/tasks/display/image_buffers/` directory)
image_name = "BrownLogo"
image_path = "/Users/nachoblancasrodriguez/Documents/image_brown.png" # Absolute path to the image you want to convert
buffer = image_to_buffer(image_path)
export_buffer(buffer, image_name)
visualize_buffer(buffer)