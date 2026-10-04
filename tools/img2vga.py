"""Convert an image to a 640x480 1-bit VGA frame (80 bytes/row, MSB = leftmost pixel).

Usage: python tools/img2vga.py tools/robot.png include/robot_vga.h robot_vga
Needs: pip install pillow
"""
import sys
from PIL import Image, ImageOps

W, H = 640, 480

src, dst, name = sys.argv[1], sys.argv[2], sys.argv[3]

img = Image.open(src).convert("RGBA")
bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
gray = Image.alpha_composite(bg, img).convert("L")

# Dark ink / dim areas become lit green pixels on the black screen.
gray = ImageOps.invert(gray)
gray = ImageOps.contain(gray, (W, H), Image.LANCZOS)

canvas = Image.new("L", (W, H), 0)
canvas.paste(gray, ((W - gray.width) // 2, (H - gray.height) // 2))

bits = canvas.convert("1")  # Floyd-Steinberg dithering
px = bits.load()

data = bytearray()
for y in range(H):
    for xb in range(W // 8):
        b = 0
        for i in range(8):
            b = (b << 1) | (1 if px[xb * 8 + i, y] else 0)
        data.append(b)

with open(dst, "w") as f:
    f.write("#pragma once\n#include <stdint.h>\n\n")
    f.write(f"// {W}x{H}, 1 bpp, {len(data)} bytes\n")
    f.write(f"const uint8_t {name}[{len(data)}] = {{\n")
    for i in range(0, len(data), 16):
        f.write("  " + ", ".join(f"0x{v:02X}" for v in data[i:i + 16]) + ",\n")
    f.write("};\n")

print(f"wrote {dst}: {len(data)} bytes")
