#!/usr/bin/env python3
"""Write a 1280x800 XRGB8888 boot picture, gzip-compressed.

The panel stride on this tablet is 5120 bytes (1280 pixels, 32 bpp).
Bytes in memory are B, G, R, X. The picture is Haltia's sign (ADR 0004),
the Karelian rhombus with horns, on the screen of the build's device (a
tablet here), and the word Haltia in Golos Text, on a dark field. Flat colors keep the gzip small enough
for the BOOT partition.
"""
import gzip
import os
import sys

W = 1280
H = 800
STRIDE = W * 4

FIELD = (0x1E, 0x1D, 0x1B)
HOUSE = (0xEE, 0xE8, 0xDC)
WINDOW = (0xF6, 0xBB, 0x3C)


def px(rgb):
    r, g, b = rgb
    return bytes((b, g, r, 0xFF))


def read_pgm(path):
    """The wordmark: a binary PGM (P5), coverage 0..255 (tools/render-wordmark.py)."""
    data = open(path, "rb").read()
    parts = data.split(maxsplit=4)
    if parts[0] != b"P5":
        raise SystemExit(path + ": not a binary PGM")
    w, h = int(parts[1]), int(parts[2])
    return w, h, parts[4][:w * h]


def mix(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


# The device around the sign: none (Haltia itself), tablet, monitor or
# laptop; a Pertin Haltia build picks its own (build-safe-image.sh).
DEVICE = "tablet"
WORDMARK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "haltia-wordmark.pgm")


def main():
    global DEVICE
    out = sys.argv[1]
    if len(sys.argv) > 2:
        DEVICE = sys.argv[2]
    row = px(FIELD) * W
    buf = bytearray(row * H)

    def rect(x0, y0, x1, y1, rgb):
        p = px(rgb)
        for y in range(max(0, y0), min(H, y1)):
            o = (y * W + max(0, x0)) * 4
            buf[o:o + (min(W, x1) - max(0, x0)) * 4] = p * (min(W, x1) - max(0, x0))

    def rhombus(cx, cy, r, w, rgb):
        """A rhombus outline w thick (w=0: filled)."""
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r, cx + r + 1):
                d = abs(x - cx) + abs(y - cy)
                if d <= r and (w == 0 or d > r - w):
                    o = (y * W + x) * 4
                    buf[o:o + 4] = px(rgb)

    def horns(cx, cy, r, w, h, rgb):
        """Karelian horns at each corner, symmetric (never turning one way
        round, which would read as a pinwheel): a stem out, a bar across,
        its ends bent back toward the rhombus."""
        for dx, dy in ((0, -1), (1, 0), (0, 1), (-1, 0)):
            x, y = cx + dx * r, cy + dy * r
            x1, y1 = x + dx * h, y + dy * h
            px_, py_ = -dy, dx
            segs = (((x, y), (x1, y1)),
                    ((x1 + px_ * h, y1 + py_ * h), (x1 - px_ * h, y1 - py_ * h)),
                    ((x1 + px_ * h, y1 + py_ * h),
                     (x1 + px_ * h - dx * h * 3 // 5, y1 + py_ * h - dy * h * 3 // 5)),
                    ((x1 - px_ * h, y1 - py_ * h),
                     (x1 - px_ * h - dx * h * 3 // 5, y1 - py_ * h - dy * h * 3 // 5)))
            for (ax, ay), (bx, by) in segs:
                rect(min(ax, bx) - w // 2, min(ay, by) - w // 2,
                     max(ax, bx) + w // 2 + 1, max(ay, by) + w // 2 + 1, rgb)

    def rrect(x0, y0, x1, y1, r, rgb):
        """A rectangle with rounded corners."""
        for y in range(y0, y1):
            dy = max(y0 + r - y, y - (y1 - 1 - r), 0)
            inset = r - int((r * r - dy * dy) ** 0.5) if dy else 0
            rect(x0 + inset, y, x1 - inset, y + 1, rgb)

    def amulet(cx, cy, r):
        """Haltia's sign: the rhombus with horns, a light at its heart."""
        w = max(4, r * 23 // 100)
        rhombus(cx, cy, r, w, WINDOW)
        horns(cx, cy, r, w * 9 // 10, r * 33 // 100, WINDOW)
        rhombus(cx, cy, r * 37 // 100, 0, HOUSE)

    cx, word_y = 640, 570
    dy = 70  # the picture's middle, now that nothing lies below the word
    if DEVICE == "tablet":
        rrect(cx - 250, 110 + dy, cx + 250, 470 + dy, 34, HOUSE)
        rect(cx - 222, 136 + dy, cx + 222, 444 + dy, FIELD)
        amulet(cx, 290 + dy, 84)
    elif DEVICE == "monitor":
        rrect(cx - 250, 90 + dy, cx + 250, 420 + dy, 18, HOUSE)
        rect(cx - 226, 112 + dy, cx + 226, 398 + dy, FIELD)
        rect(cx - 38, 420 + dy, cx + 38, 452 + dy, HOUSE)
        rrect(cx - 130, 448 + dy, cx + 130, 470 + dy, 8, HOUSE)
        amulet(cx, 255 + dy, 80)
    elif DEVICE == "laptop":
        rrect(cx - 200, 90 + dy, cx + 200, 390 + dy, 16, HOUSE)
        rect(cx - 178, 112 + dy, cx + 178, 370 + dy, FIELD)
        for y in range(390 + dy, 446 + dy):
            k = (y - 390 - dy) * 30 // 56
            rect(cx - 280 + k, y, cx + 280 - k, y + 1, HOUSE)
        rect(cx - 50, 390 + dy, cx + 50, 402 + dy, FIELD)
        amulet(cx, 241 + dy, 72)
    else:
        amulet(cx, 268 + dy, 120)

    # The word: Haltia in Golos Text, blended in the house's color.
    ww, wh, cov = read_pgm(WORDMARK)
    wx = cx - ww // 2
    for y in range(wh):
        for x in range(ww):
            c = cov[y * ww + x]
            if c:
                o = ((word_y + y) * W + wx + x) * 4
                buf[o:o + 4] = px(mix(FIELD, HOUSE, c / 255))

    if len(buf) != H * STRIDE:
        raise SystemExit("bad stride")
    with gzip.open(out, "wb", compresslevel=9) as fh:
        fh.write(buf)


if __name__ == "__main__":
    main()
