#!/usr/bin/env python3
"""Split a raw ext4 image into Android sparse pieces the old MT6589 LK accepts.

LK 0.5 on the B8000-H only understands RAW and DONT_CARE chunks ("Unknown
chunk type" for FILL/CRC) and its download buffer is < 64 MiB. Each piece
describes the whole partition: DONT_CARE up to its range, one RAW chunk with
the data (zero blocks included, so no stale eMMC data survives inside the
filesystem), DONT_CARE for the rest. Flash the pieces in order.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

BLK = 4096
SPARSE_MAGIC = 0xED26FF3A
RAW, DONT_CARE = 0xCAC1, 0xCAC3
FILE_HDR = 28
CHUNK_HDR = 12


def chunk(kind: int, blocks: int, data_len: int) -> bytes:
    return struct.pack('<HHII', kind, 0, blocks, CHUNK_HDR + data_len)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('raw', type=Path)
    parser.add_argument('outdir', type=Path)
    parser.add_argument('--piece-mib', type=int, default=15)
    args = parser.parse_args()

    size = args.raw.stat().st_size
    if size % BLK:
        raise SystemExit('raw image size is not a multiple of 4096')
    total = size // BLK
    per = args.piece_mib * 1024 * 1024 // BLK
    args.outdir.mkdir(parents=True, exist_ok=True)
    for old in args.outdir.glob('piece-*.simg'):
        old.unlink()

    whole = hashlib.sha256()
    with args.raw.open('rb') as src:
        index = 0
        for start in range(0, total, per):
            count = min(per, total - start)
            data = src.read(count * BLK)
            whole.update(data)
            chunks = []
            if start:
                chunks.append(chunk(DONT_CARE, start, 0))
            chunks.append(chunk(RAW, count, len(data)) + data)
            rest = total - start - count
            if rest:
                chunks.append(chunk(DONT_CARE, rest, 0))
            header = struct.pack('<IHHHHIIII', SPARSE_MAGIC, 1, 0, FILE_HDR,
                                 CHUNK_HDR, BLK, total, len(chunks), 0)
            out = args.outdir / f'piece-{index:03d}.simg'
            out.write_bytes(header + b''.join(chunks))
            index += 1
    print(f'blocks={total} pieces={index} raw_sha256={whole.hexdigest()}')


if __name__ == '__main__':
    main()
