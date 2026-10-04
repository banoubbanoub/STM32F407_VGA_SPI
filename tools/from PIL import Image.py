import os
from PIL import Image

# Exact path to your desktop image
image_path = r"C:\Users\pc\Desktop\Screenshot 2026-09-27 112152.png"
output_h_path = r"C:\Users\pc\Desktop\robot_bitmap.h"

def convert_image_to_cpp_array(input_path, output_path, width=640, height=480, threshold=180, invert=True):
    if not os.path.exists(input_path):
        print(f"Error: File not found at {input_path}")
        return

    # Load and convert image to grayscale
    img = Image.open(input_path).convert('L')

    # Fit image onto 640x480 canvas while preserving aspect ratio
    img.thumbnail((width, height), Image.Resampling.LANCZOS)
    canvas = Image.new('L', (width, height), 255) # White background
    paste_x = (width - img.width) // 2
    paste_y = (height - img.height) // 2
    canvas.paste(img, (paste_x, paste_y))

    pixels = canvas.load()
    byte_array = []

    # Pack 8 horizontal pixels into 1 byte (MSB first)
    for y in range(height):
        for x_byte in range(0, width, 8):
            byte_val = 0
            for bit in range(8):
                x = x_byte + bit
                if x < width:
                    # Dark lines/outlines become active pixels
                    is_pixel = (pixels[x, y] < threshold) if invert else (pixels[x, y] >= threshold)
                    if is_pixel:
                        byte_val |= (0x80 >> bit)
            byte_array.append(byte_val)

    # Export C++ Header File
    with open(output_path, 'w') as f:
        f.write('#ifndef ROBOT_BITMAP_H\n#define ROBOT_BITMAP_H\n\n')
        f.write('#include <stdint.h>\n\n')
        f.write(f'// Dimensions: {width}x{height} pixels | Stride: {width//8} bytes/line | Total: {len(byte_array)} bytes\n')
        f.write(f'const uint8_t robot_bitmap[{len(byte_array)}] = {{\n')

        for i, b in enumerate(byte_array):
            if i % 16 == 0:
                f.write('    ')
            f.write(f'0x{b:02X}, ')
            if (i + 1) % 16 == 0:
                f.write('\n')

        f.write('\n};\n\n#endif // ROBOT_BITMAP_H\n')

    print(f"Success! Generated C++ header at: {output_path}")

# Run conversion
convert_image_to_cpp_array(image_path, output_h_path)