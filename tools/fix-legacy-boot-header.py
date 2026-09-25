#!/usr/bin/env python3
"""Restore legacy header fields that modern mkbootimg normalizes away."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--second-addr", type=lambda value: int(value, 0), required=True)
    args = parser.parse_args()

    image = bytearray(args.image.read_bytes())
    if image[:8] != b"ANDROID!":
        raise SystemExit("not a legacy Android boot image")
    if len(image) < 608:
        raise SystemExit("truncated Android boot header")

    struct.pack_into("<I", image, 28, args.second_addr)
    args.image.write_bytes(image)


if __name__ == "__main__":
    main()
