#!/usr/bin/env python3
"""Validate the identity and basic integrity of the B8000-H stock ZIP."""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path


REQUIRED_SUFFIXES = (
    "/target_bin/MT6589_Android_scatter_emmc.txt",
    "/target_bin/preloader_blade10_row_3gdata.bin",
    "/target_bin/kernel_blade10_row_3gdata.bin",
    "/target_bin/boot.img",
    "/target_bin/recovery.img",
    "/target_bin/system.img",
    "/target_bin/secro.img",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    archive = args.archive.resolve()

    if not archive.is_file():
        print(f"ERROR: not a file: {archive}", file=sys.stderr)
        return 1

    print(f"archive={archive}")
    print(f"size={archive.stat().st_size}")
    print(f"sha256={sha256(archive)}")

    if zipfile.is_zipfile(archive):
        return inspect_zip(archive)
    with archive.open("rb") as stream:
        magic = stream.read(8)
    if magic.startswith(b"Rar!\x1a\x07"):
        return inspect_rar(archive)
    print("ERROR: unsupported archive format", file=sys.stderr)
    return 2


def inspect_zip(archive: Path) -> int:
    with zipfile.ZipFile(archive) as package:
        bad = package.testzip()
        if bad:
            print(f"ERROR: corrupt ZIP member: {bad}", file=sys.stderr)
            return 3
        names = package.namelist()
        selected: dict[str, str] = {}
        for suffix in REQUIRED_SUFFIXES:
            matches = [name for name in names if name.replace("\\", "/").endswith(suffix)]
            if len(matches) != 1:
                print(f"ERROR: expected one {suffix}, found {len(matches)}", file=sys.stderr)
                return 4
            selected[suffix] = matches[0]

        scatter_name = selected[REQUIRED_SUFFIXES[0]]
        scatter = package.read(scatter_name).decode("utf-8", errors="replace")
        if "MT6589" not in scatter:
            print("ERROR: scatter does not identify MT6589 family", file=sys.stderr)
            return 5
        if not re.search(r"blade10_row_3gdata", "\n".join(names), re.IGNORECASE):
            print("ERROR: archive lacks blade10_row_3gdata markers", file=sys.stderr)
            return 6

        print("zip_test=OK")
        print("board_marker=blade10_row_3gdata")
        print(f"members={len(names)}")
        print("required_members:")
        for suffix, name in selected.items():
            info = package.getinfo(name)
            print(f"  {name} size={info.file_size} crc32={info.CRC:08x}")

        print("scatter_partitions:")
        for line in scatter.splitlines():
            stripped = line.strip()
            if stripped.startswith("partition_name:") or stripped.startswith("linear_start_addr:"):
                print(f"  {stripped}")

    return 0


def find_7z() -> str:
    candidates = (
        shutil.which("7z"),
        r"C:\Program Files\7-Zip\7z.exe",
        r"C:\Program Files (x86)\7-Zip\7z.exe",
    )
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    raise RuntimeError("7-Zip is required to inspect a RAR archive")


def inspect_rar(archive: Path) -> int:
    seven_zip = find_7z()
    test = subprocess.run(
        [seven_zip, "t", "-bso0", "-bse1", str(archive)],
        check=False,
        text=True,
        capture_output=True,
    )
    if test.returncode:
        print(test.stderr, file=sys.stderr)
        return 3

    listing = subprocess.run(
        [seven_zip, "l", "-slt", str(archive)],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.replace("\\", "/")
    paths = [line[7:] for line in listing.splitlines() if line.startswith("Path = ")]
    selected: dict[str, str] = {}
    for suffix in REQUIRED_SUFFIXES:
        matches = [name for name in paths if name.endswith(suffix)]
        if len(matches) != 1:
            print(f"ERROR: expected one {suffix}, found {len(matches)}", file=sys.stderr)
            return 4
        selected[suffix] = matches[0]

    scatter_name = selected[REQUIRED_SUFFIXES[0]]
    scatter = subprocess.run(
        [seven_zip, "e", "-so", str(archive), scatter_name],
        check=True,
        capture_output=True,
    ).stdout.decode("utf-8", errors="replace")
    if "MT6589" not in scatter and "PRELOADER 0x0" not in scatter:
        print("ERROR: scatter does not identify the expected MT6589 layout", file=sys.stderr)
        return 5
    if not re.search(r"blade10_row_3gdata", listing, re.IGNORECASE):
        print("ERROR: archive lacks blade10_row_3gdata markers", file=sys.stderr)
        return 6

    print("archive_test=OK")
    print("archive_type=RAR4")
    print("board_marker=blade10_row_3gdata")
    print(f"members={len(paths) - 1}")
    print("required_members:")
    for name in selected.values():
        print(f"  {name}")
    print("scatter_map:")
    for line in scatter.splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("{") and not stripped.startswith("}"):
            print(f"  {stripped}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
