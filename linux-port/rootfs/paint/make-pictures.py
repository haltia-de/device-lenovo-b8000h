#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Draw the built-in colour-by-number pictures for b8k-paint.

Each picture is drawn straight onto a grid with a small palette, so the
cells are clean: level 1 on 16x12 cells, level 2 on 32x24. The harder
levels 3 and 4 come from paintings, see make-paintings.py. Output: one
JSON file per picture in pictures/ next to this script:
  {"name": ..., "theme": ..., "level": 1, "w": 16, "h": 12,
   "palette": ["#rrggbb", ...], "rows": ["0123...", ...]}
(one base-36 digit per cell, numbered in palette order)
Run it again after changing a drawing; the JSON files are committed.
"""
import json
import math
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pictures")


class Canvas:
    def __init__(self, palette, w=32, h=24, bg=0):
        self.palette = palette
        self.w, self.h = w, h
        self.g = [[bg] * w for _ in range(h)]

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.g[y][x] = c

    def rect(self, x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y, c)

    def ellipse(self, cx, cy, rx, ry, c):
        for y in range(self.h):
            for x in range(self.w):
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                    self.put(x, y, c)

    def circle(self, cx, cy, r, c):
        self.ellipse(cx, cy, r, r, c)

    def poly(self, pts, c):
        # Fill cells whose centre lies inside the polygon.
        n = len(pts)
        for y in range(self.h):
            for x in range(self.w):
                px, py = x + 0.5, y + 0.5
                inside = False
                for i in range(n):
                    x1, y1 = pts[i]
                    x2, y2 = pts[(i + 1) % n]
                    if (y1 > py) != (y2 > py):
                        xi = x1 + (py - y1) * (x2 - x1) / (y2 - y1)
                        if px < xi:
                            inside = not inside
                if inside:
                    self.put(x, y, c)

    def line(self, x0, y0, x1, y1, c):
        steps = max(abs(x1 - x0), abs(y1 - y0)) * 2 + 1
        for i in range(steps + 1):
            t = i / steps
            self.put(round(x0 + (x1 - x0) * t), round(y0 + (y1 - y0) * t), c)

    def save(self, slug, name, theme, level=2):
        used = sorted({c for row in self.g for c in row})
        remap = {old: new for new, old in enumerate(used)}
        palette = [self.palette[c] for c in used]
        rows = ["".join("0123456789abcdefghijklmnopqrstuvwxyz"[remap[c]] for c in row)
                for row in self.g]
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, slug + ".json"), "w") as fh:
            json.dump({"name": name, "theme": theme, "level": level, "w": self.w,
                       "h": self.h, "palette": palette, "rows": rows},
                      fh, ensure_ascii=False, indent=0)
            fh.write("\n")


def house():
    c = Canvas(["#9ad0f5", "#f6d32d", "#e66100", "#c64600", "#f6f5f4",
                "#865e3c", "#57e389", "#26a269", "#3584e4"])
    c.circle(4, 4, 3, 1)
    c.rect(0, 18, 31, 23, 6)
    c.rect(7, 11, 20, 19, 4)
    c.poly([(5, 11.5), (13.5, 4), (22, 11.5)], 3)
    c.rect(12, 14, 15, 19, 5)
    c.rect(8, 13, 10, 15, 8)
    c.rect(17, 13, 19, 15, 8)
    c.rect(25, 12, 26, 19, 5)
    c.circle(25.5, 10, 3.5, 7)
    c.save("house", "Домик", "Природа")


def flower():
    c = Canvas(["#f6f5f4", "#e01b24", "#f6d32d", "#26a269", "#57e389",
                "#c64600", "#865e3c"])
    c.rect(15, 11, 16, 18, 3)
    c.ellipse(11.5, 14.5, 3, 1.4, 4)
    c.ellipse(19.5, 13, 3, 1.4, 4)
    for a in range(6):
        ang = a * math.pi / 3
        c.circle(15.5 + 4.2 * math.cos(ang), 7 + 4.2 * math.sin(ang), 2.6, 1)
    c.circle(15.5, 7, 2.4, 2)
    c.poly([(9, 18.5), (23, 18.5), (21, 23.5), (11, 23.5)], 5)
    c.rect(8, 18, 23, 19, 6)
    c.save("flower", "Цветок", "Природа")


def fish():
    c = Canvas(["#1c71d8", "#99c1f1", "#ff7800", "#ffa348", "#f6f5f4",
                "#241f31", "#33d17a", "#f6d32d"])
    c.rect(0, 18, 31, 23, 7)
    for x in (3, 7, 27):
        c.rect(x, 12, x, 18, 6)
        c.put(x - 1, 14, 6)
        c.put(x + 1, 16, 6)
    c.ellipse(15, 11, 9, 5.5, 2)
    c.poly([(5, 11), (0.5, 6), (0.5, 16)], 3)
    c.poly([(12, 6), (17, 3), (19, 6.5)], 3)
    c.ellipse(15, 12.5, 6, 2, 3)
    c.circle(20.5, 9.5, 1.6, 4)
    c.put(21, 9, 5)
    c.circle(26, 4, 1, 1)
    c.circle(28, 7, 0.8, 1)
    c.save("fish", "Рыбка", "Животные")


def boat():
    c = Canvas(["#99c1f1", "#1c71d8", "#f6f5f4", "#e01b24", "#865e3c",
                "#f6d32d", "#62a0ea"])
    c.circle(5, 4, 2.5, 5)
    c.rect(0, 17, 31, 23, 1)
    for x in range(0, 32, 4):
        c.put(x, 17, 6)
        c.put(x + 1, 16, 6)
    c.poly([(16, 2), (16, 14), (25, 14)], 2)
    c.poly([(15, 4), (15, 14), (8, 14)], 2)
    c.rect(15, 2, 15, 15, 4)
    c.poly([(7, 15), (26, 15), (23, 18.5), (10, 18.5)], 3)
    c.save("boat", "Кораблик", "Флот")


def butterfly():
    c = Canvas(["#f6f5f4", "#9141ac", "#c061cb", "#f6d32d", "#241f31",
                "#57e389"])
    c.rect(0, 20, 31, 23, 5)
    c.ellipse(10.5, 8, 5.5, 5, 1)
    c.ellipse(21.5, 8, 5.5, 5, 1)
    c.ellipse(11, 15.5, 4, 3.5, 2)
    c.ellipse(21, 15.5, 4, 3.5, 2)
    c.circle(10, 8, 2, 3)
    c.circle(22, 8, 2, 3)
    c.rect(15, 5, 16, 18, 4)
    c.line(15, 5, 12, 1, 4)
    c.line(16, 5, 19, 1, 4)
    c.save("butterfly", "Бабочка", "Животные")


def mushroom():
    c = Canvas(["#deddda", "#e01b24", "#f6f5f4", "#f9f06b", "#26a269",
                "#57e389", "#865e3c"])
    c.rect(0, 19, 31, 23, 4)
    for x in range(1, 31, 3):
        c.rect(x, 17, x, 19, 5)
    c.rect(13, 12, 18, 20, 3)
    c.ellipse(15.5, 11, 11, 7, 1)
    c.rect(4, 11, 27, 13, 0)
    c.rect(4, 12, 27, 12, 0)
    c.ellipse(15.5, 11, 11, 7, 1)
    c.rect(3, 12, 28, 18, 0)
    c.rect(13, 12, 18, 20, 3)
    c.rect(0, 20, 31, 23, 4)
    for (x, y, r) in ((10, 7, 1.5), (17, 5, 1.6), (22, 9, 1.5), (13, 10, 1.1)):
        c.circle(x, y, r, 2)
    c.save("mushroom", "Грибок", "Природа")


def apple():
    c = Canvas(["#f6f5f4", "#e01b24", "#a51d2d", "#26a269", "#865e3c",
                "#f66151"])
    c.ellipse(11.5, 13.5, 7, 8, 1)
    c.ellipse(19.5, 13.5, 7, 8, 1)
    c.ellipse(16, 20, 6, 3, 2)
    c.ellipse(10, 10, 2, 3, 5)
    c.rect(15, 2, 16, 6, 4)
    c.ellipse(20, 4, 3.5, 1.6, 3)
    c.save("apple", "Яблоко", "Природа")


def cat():
    c = Canvas(["#f6f5f4", "#ff7800", "#ffa348", "#241f31", "#f66151",
                "#57e389"])
    c.poly([(6, 9), (8, 1), (13, 6)], 1)
    c.poly([(26, 9), (24, 1), (19, 6)], 1)
    c.poly([(7.5, 7), (8.5, 3), (11, 6)], 4)
    c.poly([(24.5, 7), (23.5, 3), (21, 6)], 4)
    c.ellipse(16, 13, 11, 9, 1)
    c.ellipse(16, 17, 6, 4, 2)
    c.ellipse(11, 12, 2, 2.4, 5)
    c.ellipse(21, 12, 2, 2.4, 5)
    c.rect(11, 11, 11, 13, 3)
    c.rect(21, 11, 21, 13, 3)
    c.poly([(14.5, 15), (17.5, 15), (16, 17)], 4)
    c.line(3, 15, 10, 16, 3)
    c.line(3, 18, 10, 17, 3)
    c.line(29, 15, 22, 16, 3)
    c.line(29, 18, 22, 17, 3)
    c.save("cat", "Котик", "Животные")


def rainbow():
    c = Canvas(["#99c1f1", "#e01b24", "#ff7800", "#f6d32d", "#33d17a",
                "#3584e4", "#9141ac", "#26a269", "#f6f5f4"])
    for i, col in enumerate((1, 2, 3, 4, 5, 6)):
        c.ellipse(16, 22, 15 - i * 1.6, 15 - i * 1.6, col)
    c.ellipse(16, 22, 5.4, 5.4, 0)
    c.ellipse(5, 4, 3.5, 1.6, 8)
    c.ellipse(26, 6, 4, 1.8, 8)
    c.ellipse(6, 26, 12, 8, 7)
    c.ellipse(26, 27, 12, 8, 7)
    c.save("rainbow", "Радуга", "Природа")


def tree():
    c = Canvas(["#f6f5f4", "#26a269", "#57e389", "#865e3c", "#e01b24",
                "#f6d32d", "#99c1f1"])
    c.rect(0, 0, 31, 23, 6)
    c.rect(0, 20, 31, 23, 0)
    c.poly([(16, 1), (7, 9), (25, 9)], 1)
    c.poly([(16, 5), (5, 14), (27, 14)], 1)
    c.poly([(16, 10), (3, 19), (29, 19)], 1)
    c.rect(14, 19, 17, 21, 3)
    for (x, y) in ((13, 8), (19, 12), (10, 16), (21, 17), (16, 14)):
        c.put(x, y, 4)
    c.poly([(16, 0), (15, 2), (17, 2)], 5)
    c.put(16, 1, 5)
    for (x, y) in ((3, 3), (28, 4), (5, 10), (26, 11), (2, 17)):
        c.put(x, y, 0)
    c.save("tree", "Ёлочка", "Северная природа")


# --- level 2 additions ---
def cruiser():
    c = Canvas(["#99c1f1", "#1c71d8", "#62a0ea", "#5e5c64", "#9a9996",
                "#deddda", "#c01c28", "#f6f5f4", "#3584e4"])
    c.ellipse(6, 4, 3.5, 1.5, 7)
    c.ellipse(27, 6, 3, 1.3, 7)
    c.rect(0, 16, 31, 23, 1)
    for y in (18, 21):
        for x in range(y % 4, 32, 5):
            c.rect(x, y, x + 1, y, 2)
    c.poly([(1, 12), (31, 12), (28, 17), (4, 17)], 3)
    c.rect(4, 16, 27, 16, 6)
    c.rect(10, 9, 22, 11, 4)
    c.rect(13, 6, 19, 8, 4)
    c.rect(14, 7, 18, 7, 5)
    c.rect(16, 1, 16, 5, 3)
    c.rect(11, 5, 12, 8, 3)
    c.rect(20, 5, 21, 8, 3)
    c.rect(4, 10, 7, 11, 4)
    c.rect(0, 10, 3, 10, 3)
    c.rect(24, 10, 27, 11, 4)
    c.rect(28, 10, 31, 10, 3)
    c.rect(17, 1, 19, 3, 7)
    for (x, y) in ((17, 1), (19, 1), (18, 2), (17, 3), (19, 3)):
        c.put(x, y, 8)
    c.save("cruiser", "Крейсер", "Военный флот")


def lighthouse():
    c = Canvas(["#99c1f1", "#1c71d8", "#5e5c64", "#f6f5f4", "#e01b24",
                "#f6d32d", "#241f31", "#f9f06b"])
    c.poly([(13, 3), (31.5, -1), (31.5, 8)], 7)
    c.rect(0, 17, 31, 23, 1)
    c.ellipse(10, 21, 10, 5, 2)
    c.poly([(7, 19), (13, 19), (12, 6), (8, 6)], 3)
    c.poly([(7.7, 9), (12.3, 9), (12.5, 11), (7.5, 11)], 4)
    c.poly([(7.3, 14), (12.7, 14), (12.9, 16), (7.1, 16)], 4)
    c.rect(8, 4, 12, 5, 5)
    c.poly([(7, 4), (13, 4), (10, 1)], 6)
    c.rect(7, 6, 13, 6, 6)
    c.poly([(25, 11), (25, 16), (29, 16)], 3)
    c.rect(22, 16, 29, 16, 4)
    c.save("lighthouse", "Маяк", "Флот")


def polar_bear():
    c = Canvas(["#3584e4", "#1a5fb4", "#f6f5f4", "#f9e7b5", "#e5c98e",
                "#241f31"])
    c.rect(0, 16, 31, 23, 1)
    c.poly([(23, 16), (27, 7), (31.5, 9), (31.5, 16)], 2)
    c.poly([(-0.5, 15), (22, 15), (25, 19.5), (-0.5, 19.5)], 2)
    c.ellipse(11, 11, 7, 3.5, 3)
    for x in (5, 9, 13, 16):
        c.rect(x, 13, x + 1, 15, 3)
    c.ellipse(11, 13, 5, 1.2, 4)
    c.ellipse(19, 10, 3, 2.2, 3)
    c.rect(21, 10, 22, 11, 3)
    c.put(18, 8, 4)
    c.put(22, 10, 5)
    c.put(19, 9, 5)
    c.save("polar-bear", "Белый медведь", "Животные")


def aurora():
    c = Canvas(["#241f31", "#33d17a", "#8ff0a4", "#9141ac", "#f6f5f4",
                "#26a269", "#f9f06b", "#c0bfbc"])
    for x in range(32):
        y = round(7 + 3 * math.sin(x / 5.0))
        c.put(x, y - 3, 3)
        c.rect(x, y - 2, x, y + 2, 1)
        c.rect(x, y - 1, x, y, 2)
    for (x, y) in ((2, 1), (9, 2), (20, 1), (28, 3), (14, 13), (19, 15), (2, 12)):
        c.put(x, y, 6)
    c.ellipse(26, 25, 14, 6, 7)
    c.ellipse(8, 24, 16, 5, 4)
    for (x, y) in ((4, 20), (9, 19), (24, 21), (29, 20)):
        c.poly([(x + 0.5, y - 8), (x - 2.5, y + 0.5), (x + 3.5, y + 0.5)], 5)
    c.save("aurora", "Северное сияние", "Северная природа")


def owl():
    c = Canvas(["#1a5fb4", "#f9f06b", "#4a3020", "#986a44", "#f6d9a8",
                "#f6f5f4", "#241f31", "#ff7800", "#26a269"])
    c.circle(25, 5, 3.5, 1)
    c.rect(0, 19, 31, 20, 2)
    c.ellipse(4, 18, 2.5, 1, 8)
    c.ellipse(28, 18, 2.5, 1, 8)
    c.poly([(10, 7), (11, 3), (13.5, 7)], 3)
    c.poly([(21, 7), (20, 3), (17.5, 7)], 3)
    c.ellipse(15.5, 13, 6, 7, 3)
    c.ellipse(15.5, 15.5, 3.5, 4, 4)
    c.circle(13, 10, 2, 5)
    c.circle(18, 10, 2, 5)
    c.put(13, 10, 6)
    c.put(18, 10, 6)
    c.poly([(14.5, 11), (16.5, 11), (15.5, 13.6)], 7)
    for x in (13, 14, 17, 18):
        c.put(x, 19, 7)
    c.save("owl", "Сова", "Животные")


# --- level 1: 16x12 cells, five colours at most ---
def easy_sailboat():
    c = Canvas(["#99c1f1", "#1c71d8", "#f6f5f4", "#e01b24", "#865e3c"], 16, 12)
    c.rect(0, 9, 15, 11, 1)
    c.poly([(8, 1), (8, 8), (13, 8)], 2)
    c.poly([(7, 2), (7, 8), (3, 8)], 2)
    c.rect(7, 1, 7, 8, 4)
    c.poly([(2, 8), (14, 8), (12.5, 10.6), (3.5, 10.6)], 3)
    c.save("easy-sailboat", "Парусник", "Флот", 1)


def easy_warship():
    c = Canvas(["#99c1f1", "#1c71d8", "#77767b", "#c0bfbc", "#e01b24"], 16, 12)
    c.rect(0, 9, 15, 11, 1)
    c.poly([(0.5, 7), (15.5, 7), (13.5, 10.5), (2.5, 10.5)], 2)
    c.rect(5, 5, 10, 6, 3)
    c.rect(7, 3, 8, 4, 3)
    c.rect(8, 1, 8, 2, 2)
    c.rect(9, 1, 10, 1, 4)
    c.rect(1, 6, 3, 6, 3)
    c.rect(12, 6, 14, 6, 3)
    c.save("easy-warship", "Военный кораблик", "Военный флот", 1)


def easy_fish():
    c = Canvas(["#3584e4", "#ff7800", "#ffa348", "#f6f5f4", "#241f31"], 16, 12)
    c.ellipse(8.5, 6, 4.5, 3, 1)
    c.poly([(5, 6), (0.5, 2.5), (0.5, 9.5)], 2)
    c.poly([(7, 3.5), (9, 1.5), (10.5, 3.5)], 2)
    c.put(11, 5, 4)
    c.put(14, 2, 3)
    c.put(13, 4, 3)
    c.save("easy-fish", "Золотая рыбка", "Животные", 1)


def easy_duck():
    c = Canvas(["#9ad0f5", "#1c71d8", "#f6d32d", "#ff7800", "#241f31"], 16, 12)
    c.rect(0, 9, 15, 11, 1)
    c.ellipse(7, 8, 5, 2.5, 2)
    c.circle(11, 4.5, 2.3, 2)
    c.rect(13, 5, 14, 5, 3)
    c.put(11, 4, 4)
    c.save("easy-duck", "Уточка", "Животные", 1)


def easy_fir():
    c = Canvas(["#99c1f1", "#f6f5f4", "#26a269", "#865e3c", "#f6d32d"], 16, 12)
    c.rect(0, 9, 15, 11, 1)
    c.poly([(8, 0.5), (4, 5), (12, 5)], 2)
    c.poly([(8, 3), (3, 9), (13, 9)], 2)
    c.rect(7, 9, 8, 10, 3)
    c.rect(7, 0, 8, 0, 4)
    for (x, y) in ((1, 2), (14, 1), (2, 6), (13, 4)):
        c.put(x, y, 1)
    c.save("easy-fir", "Ёлка в снегу", "Северная природа", 1)


def easy_tulip():
    c = Canvas(["#f6f5f4", "#e01b24", "#26a269", "#57e389", "#a51d2d"], 16, 12)
    c.rect(7, 6, 8, 11, 2)
    c.ellipse(4.5, 9, 2.6, 1.5, 3)
    c.ellipse(11, 8, 2.6, 1.5, 3)
    c.ellipse(8, 4, 3, 2.5, 1)
    c.poly([(4.5, 0.5), (6, 3), (8, 0.5), (10, 3), (11.5, 0.5), (11.5, 4), (4.5, 4)], 1)
    c.rect(7, 2, 8, 5, 4)
    c.save("easy-tulip", "Тюльпан", "Природа", 1)


if __name__ == "__main__":
    for draw in (easy_sailboat, easy_warship, easy_fish, easy_duck, easy_fir,
                 easy_tulip, house, flower, fish, boat, butterfly, mushroom,
                 apple, cat, rainbow, tree, cruiser, lighthouse, polar_bear,
                 aurora, owl):
        draw()
    print("pictures in", OUT)
