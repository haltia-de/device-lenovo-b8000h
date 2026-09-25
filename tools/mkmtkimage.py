#!/usr/bin/env python3
"""Prefix a payload with the legacy 512-byte MediaTek image header."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


MTK_MAGIC = 0x58881688
HEADER_SIZE = 512
NAME_SIZE = 32


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("label")
    args = parser.parse_args()

    payload = args.input.read_bytes()
    label = args.label.encode("ascii")
    if not label or len(label) >= NAME_SIZE:
        raise SystemExit("label must be 1..31 ASCII bytes")

    header = bytearray(b"\xff" * HEADER_SIZE)
    struct.pack_into("<II", header, 0, MTK_MAGIC, len(payload))
    header[8 : 8 + NAME_SIZE] = label.ljust(NAME_SIZE, b"\0")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header + payload)


if __name__ == "__main__":
    main()
