#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Turn public-domain paintings into the harder b8k-paint pictures.

  make-paintings.py CACHE_DIR

downloads each painting from Wikimedia Commons (800 px wide) into
CACHE_DIR, then writes pictures/<slug>.json: level 3 at 64 cells along
the long side and up to 16 colours, level 4 at 96 cells and up to 24
colours, level 5 at 128 cells and up to 28 colours. All
works are by artists who died more than 70 years ago, so they are in the
public domain; PAINTINGS names each Commons file.
"""
import json
import os
import sys
import urllib.parse
import urllib.request

from PIL import Image, ImageEnhance, ImageFilter, ImageOps

DIGITS = "0123456789abcdefghijklmnopqrstuvwxyz"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pictures")

# slug, Commons file, name, theme, level
PAINTINGS = [
    ("golden-autumn", "Levitan Zolotaya Osen.jpg", "Золотая осень", "Природа", 3),
    ("rye", "Ivan Shishkin - Рожь - Google Art Project.jpg", "Рожь", "Природа", 3),
    ("winter", "Ivan Shishkin - Winter.JPG", "Зима", "Северная природа", 3),
    ("rainbow-sea", "Aivazovsky, Rainbow.JPG", "Радуга над морем", "Флот", 3),
    ("brig-mercury", "Aivazovsky, Brig Mercury Attacked by Two Turkish Ships 1892.jpg",
     "Бриг «Меркурий»", "Военный флот", 3),
    ("fox", "Koson - fox-in-the-reeds.jpg", "Лиса в камышах", "Животные", 3),
    ("hare", "Albrecht Dürer - Hare, 1502 - Google Art Project.jpg", "Заяц", "Животные", 3),
    ("north", "Arkhip Kuindzhi - North.jpg", "Север", "Северная природа", 4),
    ("swans", "Rylov Blue Expanse.jpg", "В голубом просторе", "Северная природа", 4),
    ("great-wave", "The Great Wave off Kanagawa.jpg", "Большая волна", "Флот", 4),
    ("ninth-wave", "Aivazovsky, Ivan - The Ninth Wave.jpg", "Девятый вал", "Флот", 5),
    ("sevastopol", "Ivan Constantinovich Aivazovsky - The Russian Squadron on the Sebastopol Roads.jpg",
     "Эскадра на Севастопольском рейде", "Военный флот", 4),
    ("feodosia", "Aivazovsky - Black Sea Fleet in the Bay of Theodosia.jpg",
     "Черноморский флот", "Военный флот", 4),
    ("twelve-apostles", 'Aivasovsky I C Ship "Twelve Apostles".jpg',
     "Корабль «Двенадцать апостолов»", "Военный флот", 4),
    ("crow", "Cawing crow by Ohara Koson.jpg", "Ворона", "Животные", 4),
    ("pine-morning", "Shishkin, Ivan - Morning in a Pine Forest.jpg",
     "Утро в сосновом лесу", "Животные", 5),
    ("monarch", "Monarch of the Glen, Edwin Landseer, 1851.jpg", "Король долины",
     "Животные", 5),
]
LEVELS = {3: (64, 16), 4: (96, 24), 5: (128, 28)}


def fetch(cache, name):
    path = os.path.join(cache, name.replace("/", "_"))
    if not os.path.exists(path):
        url = ("https://commons.wikimedia.org/wiki/Special:FilePath/"
               + urllib.parse.quote(name) + "?width=1200")
        req = urllib.request.Request(url, headers={"User-Agent": "b8k-paint/1.0"})
        with urllib.request.urlopen(req, timeout=60) as r, open(path, "wb") as fh:
            fh.write(r.read())
    return path


def merge_close(rgb, limit):
    """Indices to keep so that no two kept colours are closer than limit."""
    keep = []
    for i, c in enumerate(rgb):
        if all(sum((c[k] - rgb[j][k]) ** 2 for k in range(3)) >= limit ** 2 for j in keep):
            keep.append(i)
    return keep


def convert(path, cols_long, colours):
    img = ImageOps.exif_transpose(Image.open(path)).convert("RGB")
    # Old varnish makes paintings brown and dull; brighter, more saturated
    # colours are easier to tell apart on the palette.
    img = ImageOps.autocontrast(img, cutoff=1)
    img = ImageEnhance.Color(img).enhance(1.6)
    img = ImageEnhance.Contrast(img).enhance(1.15)
    w, h = img.size
    if w >= h:
        cols, rows = cols_long, max(8, round(cols_long * h / w))
    else:
        rows, cols = cols_long, max(8, round(cols_long * w / h))
    small = img.filter(ImageFilter.MedianFilter(3)).resize((cols, rows), Image.LANCZOS)
    q = small.quantize(colors=colours + 4, method=Image.Quantize.MEDIANCUT,
                       dither=Image.Dither.NONE)
    raw = q.getpalette()
    data = list(q.getdata())
    counts = {}
    for v in data:
        counts[v] = counts.get(v, 0) + 1
    # Most used first, then drop colours too close to one already kept.
    used = sorted(counts, key=lambda v: -counts[v])
    rgb = [tuple(raw[i * 3:i * 3 + 3]) for i in used]
    keep = [rgb[i] for i in merge_close(rgb, 36)][:colours]
    pix = list(small.getdata())

    def nearest(c):
        return min(range(len(keep)), key=lambda j: sum((c[k] - keep[j][k]) ** 2 for k in range(3)))

    grid = [[nearest(pix[y * cols + x]) for x in range(cols)] for y in range(rows)]
    # A lone cell surrounded by one other colour is noise; join it.
    for y in range(rows):
        for x in range(cols):
            near = [grid[ny][nx] for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1))
                    if 0 <= nx < cols and 0 <= ny < rows]
            if grid[y][x] not in near and len(set(near)) == 1:
                grid[y][x] = near[0]
    present = sorted({v for row in grid for v in row})
    order = sorted(present, key=lambda i: -(0.299 * keep[i][0] + 0.587 * keep[i][1]
                                            + 0.114 * keep[i][2]))
    remap = {old: n for n, old in enumerate(order)}
    return {"w": cols, "h": rows,
            "palette": ["#%02x%02x%02x" % keep[i] for i in order],
            "rows": ["".join(DIGITS[remap[v]] for v in row) for row in grid]}


def main():
    cache = sys.argv[1]
    os.makedirs(cache, exist_ok=True)
    for slug, name, title, theme, level in PAINTINGS:
        pic = convert(fetch(cache, name), *LEVELS[level])
        pic.update(name=title, theme=theme, level=level, source=name)
        with open(os.path.join(OUT, slug + ".json"), "w") as fh:
            json.dump(pic, fh, ensure_ascii=False, indent=0)
            fh.write("\n")
        print(slug, pic["w"], pic["h"], len(pic["palette"]))


if __name__ == "__main__":
    main()
