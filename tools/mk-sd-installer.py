#!/usr/bin/env python3
"""Build a raw microSD image carrying the eMMC install payload.

Layout (no partition table, read raw from the card by the installer):
  0        4 KiB text header "B8KINST1" with offsets, sizes and md5 sums
  1 MiB    gzip of the raw ext4 root image (for eMMC USRDATA)
  aligned  Linux BOOT image (for eMMC BOOTIMG)
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
from pathlib import Path

MIB = 1024 * 1024


def md5(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument('rootfs', type=Path)
    p.add_argument('bootimg', type=Path)
    p.add_argument('out', type=Path)
    a = p.parse_args()

    raw = a.rootfs.read_bytes()
    boot = a.bootimg.read_bytes()
    gz = gzip.compress(raw, compresslevel=6, mtime=0)
    gz_off = MIB
    boot_off = (gz_off + len(gz) + MIB - 1) // MIB * MIB
    header = (
        'B8KINST1\n'
        f'gz_off={gz_off}\ngz_len={len(gz)}\ngz_md5={md5(gz)}\n'
        f'raw_len={len(raw)}\nraw_md5={md5(raw)}\n'
        f'boot_off={boot_off}\nboot_len={len(boot)}\nboot_md5={md5(boot)}\n'
        'END\n'
    ).encode()
    with a.out.open('wb') as f:
        f.write(header.ljust(4096, b'\0'))
        f.seek(gz_off)
        f.write(gz)
        f.seek(boot_off)
        f.write(boot)
        end = (boot_off + len(boot) + MIB - 1) // MIB * MIB
        f.truncate(end)
    print(header.decode())


if __name__ == '__main__':
    main()
