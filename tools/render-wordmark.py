#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Render Haltia's wordmark for the boot picture (linux-port/initramfs/splash.py).

    python3 tools/render-wordmark.py GolosText[wght].ttf linux-port/initramfs/haltia-wordmark.pgm

The font is Golos Text (ParaType, SIL Open Font License 1.1,
github.com/google/fonts/tree/main/ofl/golostext); it is used here, not
shipped: the result is an 8-bit grayscale coverage map (binary PGM, P5),
which splash.py blends in the house's color. The boot build needs no font
and no Pillow. Run again only to change the word or its size.
"""
import sys

from PIL import Image, ImageDraw, ImageFont

WORD = "Haltia"
SIZE = 84
WEIGHT = b"Medium"


def main():
    font_path, out = sys.argv[1], sys.argv[2]
    font = ImageFont.truetype(font_path, SIZE)
    try:
        font.set_variation_by_name(WEIGHT)
    except (OSError, AttributeError):
        pass
    left, top, right, bottom = font.getbbox(WORD)
    w, h = right - left + 4, bottom - top + 4
    im = Image.new("L", (w, h), 0)
    ImageDraw.Draw(im).text((2 - left, 2 - top), WORD, font=font, fill=255)
    with open(out, "wb") as fh:
        fh.write(b"P5\n%d %d\n255\n" % (w, h))
        fh.write(im.tobytes())
    print(f"{out}: {w}x{h}")


if __name__ == "__main__":
    main()
