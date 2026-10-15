#!/usr/bin/env python3
"""Write a 1280x800 XRGB8888 boot picture, gzip-compressed.

The panel stride on this tablet is 5120 bytes (1280 pixels, 32 bpp).
Bytes in memory are B, G, R, X. The picture is the Linux penguin on a
black field. Flat colors keep the gzip small enough for the BOOT partition.
"""
import gzip
import sys

W = 1280
H = 800
STRIDE = W * 4

BLACK = (0x00, 0x00, 0x00)
WHITE = (0xF2, 0xF2, 0xF2)
BELLY = (0xFF, 0xFF, 0xFF)
YELLOW = (0xF0, 0xC0, 0x30)
PUPIL = (0x11, 0x11, 0x11)


def px(rgb):
    r, g, b = rgb
    return bytes((b, g, r, 0xFF))


def inside(x, y, cx, cy, rx, ry):
    dx = (x - cx) / rx
    dy = (y - cy) / ry
    return dx * dx + dy * dy <= 1.0


def edge(x, y, cx, cy, rx, ry, thickness):
    return inside(x, y, cx, cy, rx, ry) and not inside(
        x, y, cx, cy, rx - thickness, ry - thickness)


def main():
    out = sys.argv[1]
    # One black row, repeated, then the penguin is painted into a copy.
    black = px(BLACK) * W
    buf = bytearray()
    for _ in range(H):
        buf += black

    def put(x, y, rgb):
        if 0 <= x < W and 0 <= y < H:
            o = (y * W + x) * 4
            buf[o:o + 4] = px(rgb)

    # Bounds of the penguin so the black field is not scanned twice.
    x0, x1 = 450, 830
    y0, y1 = 170, 670
    for y in range(y0, y1):
        for x in range(x0, x1):
            color = None
            if edge(x, y, 640, 470, 150, 175, 5) or edge(x, y, 640, 300, 108, 112, 5):
                color = WHITE
            if edge(x, y, 500, 490, 36, 78, 5) or edge(x, y, 780, 490, 36, 78, 5):
                color = WHITE
            if inside(x, y, 640, 505, 78, 115):
                color = BELLY
            if inside(x, y, 608, 288, 18, 22) or inside(x, y, 672, 288, 18, 22):
                color = WHITE
            if inside(x, y, 614, 290, 8, 10) or inside(x, y, 678, 290, 8, 10):
                color = PUPIL
            if inside(x, y, 640, 332, 18, 12):
                color = YELLOW
            if inside(x, y, 575, 640, 42, 16) or inside(x, y, 705, 640, 42, 16):
                color = YELLOW
            if color is not None:
                put(x, y, color)
    if len(buf) != H * STRIDE:
        raise SystemExit("bad stride")
    with gzip.open(out, "wb", compresslevel=9) as fh:
        fh.write(buf)


if __name__ == "__main__":
    main()
