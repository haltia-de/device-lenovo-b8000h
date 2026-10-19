#!/usr/bin/env python3
"""Write a 1280x800 XRGB8888 boot picture, gzip-compressed.

The panel stride on this tablet is 5120 bytes (1280 pixels, 32 bpp).
Bytes in memory are B, G, R, X. The picture is Haltia's sign (ADR 0004):
a house with a lit window (someone is home) and the word Haltia on a dark
field, over a band of Karelian embroidery: rhombuses with hooks, red on
linen, drawn as cross stitches. Flat colors keep the gzip small enough
for the BOOT partition.
"""
import gzip
import sys

W = 1280
H = 800
STRIDE = W * 4

FIELD = (0x1E, 0x1D, 0x1B)
HOUSE = (0xEE, 0xE8, 0xDC)
GLOW = ((0x3A, 0x30, 0x1E), (0x5C, 0x46, 0x20), (0x8A, 0x63, 0x22))
WINDOW = (0xF6, 0xBB, 0x3C)
FRAME = (0x1E, 0x1D, 0x1B)
LINEN = (0xE4, 0xDA, 0xC4)
RED = (0xB3, 0x26, 0x2A)


def px(rgb):
    r, g, b = rgb
    return bytes((b, g, r, 0xFF))


def stitch_on(i, j):
    """One tile of the band, 17 x 17 stitches: a rhombus with hooks at its
    corners, a filled ring and a small rhombus at the heart, as Karelian
    towels have."""
    c = 8
    d = abs(i - c) + abs(j - c)
    if d == 7 or d in (3, 4) or d == 0:
        return True
    # Hooks: a stitch turned outward beside each corner of the rhombus.
    hooks = {(0, c - 1), (0, c + 1), (16, c - 1), (16, c + 1),
             (c - 1, 0), (c + 1, 0), (c - 1, 16), (c + 1, 16)}
    return (i, j) in hooks


# The word under the house, a 5 x 7 bitmap per letter.
LETTERS = {
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "a": ["00000", "00000", "01110", "00001", "01111", "10001", "01111"],
    "l": ["01100", "00100", "00100", "00100", "00100", "00100", "01110"],
    "t": ["00100", "00100", "11111", "00100", "00100", "00100", "00011"],
    "i": ["00100", "00000", "01100", "00100", "00100", "00100", "01110"],
}


def main():
    out = sys.argv[1]
    row = px(FIELD) * W
    buf = bytearray(row * H)

    def rect(x0, y0, x1, y1, rgb):
        p = px(rgb)
        for y in range(max(0, y0), min(H, y1)):
            o = (y * W + max(0, x0)) * 4
            buf[o:o + (min(W, x1) - max(0, x0)) * 4] = p * (min(W, x1) - max(0, x0))

    # The house: a roof and a body, its window lit with a warm glow around.
    cx, base = 640, 470
    bw, bh = 260, 180
    roof_h = 130
    for y in range(base - bh - roof_h, base - bh + 1):
        half = (y - (base - bh - roof_h)) * (bw // 2 + 40) // roof_h
        rect(cx - half, y, cx + half, y + 1, HOUSE)
    rect(cx - bw // 2, base - bh, cx + bw // 2, base, HOUSE)
    wx0, wy0, ww, wh = cx - 46, base - bh + 50, 92, 92
    for k, g in enumerate(GLOW):
        m = 14 - k * 5
        rect(wx0 - m, wy0 - m, wx0 + ww + m, wy0 + wh + m, g)
    rect(wx0, wy0, wx0 + ww, wy0 + wh, WINDOW)
    rect(cx - 3, wy0, cx + 3, wy0 + wh, FRAME)
    rect(wx0, wy0 + wh // 2 - 3, wx0 + ww, wy0 + wh // 2 + 3, FRAME)

    # The word: Haltia, in the house's color.
    k = 6
    word = "Haltia"
    wx = cx - (len(word) * 6 * k - k) // 2
    for n, ch in enumerate(word):
        for r, bits in enumerate(LETTERS[ch]):
            for q, b in enumerate(bits):
                if b == "1":
                    x = wx + (n * 6 + q) * k
                    rect(x, 505 + r * k, x + k, 505 + r * k + k, HOUSE)

    # The band: linen with the embroidery, cross stitches on a 6-px grid.
    s = 6
    band_y = 618
    tiles_rows = 17
    rect(0, band_y - s, W, band_y + tiles_rows * s + s, LINEN)
    cols = W // s
    for j in range(tiles_rows):
        for i in range(cols):
            if stitch_on(i % 17, j):
                x, y = i * s, band_y + j * s
                # A cross stitch: two diagonals in a cell.
                for t in range(s):
                    for x2, y2 in ((x + t, y + t), (x + s - 1 - t, y + t)):
                        for dx in (-1, 0, 1):
                            if 0 <= x2 + dx < W:
                                o = (y2 * W + x2 + dx) * 4
                                buf[o:o + 4] = px(RED)
    # Edging lines above and below the motifs.
    rect(0, band_y - s - 4, W, band_y - s - 1, RED)
    rect(0, band_y + tiles_rows * s + s + 1, W, band_y + tiles_rows * s + s + 4, RED)

    if len(buf) != H * STRIDE:
        raise SystemExit("bad stride")
    with gzip.open(out, "wb", compresslevel=9) as fh:
        fh.write(buf)


if __name__ == "__main__":
    main()
