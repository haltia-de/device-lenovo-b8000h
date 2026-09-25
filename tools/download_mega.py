#!/usr/bin/env python3
"""Download and decrypt a public MEGA file link without account credentials."""

from __future__ import annotations

import argparse
import base64
import json
import os
import secrets
import sys
import urllib.parse
import urllib.request
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


def b64url_decode(value: str) -> bytes:
    return base64.urlsafe_b64decode(value + "=" * (-len(value) % 4))


def parse_link(link: str) -> tuple[str, bytes]:
    parsed = urllib.parse.urlparse(link)
    parts = [part for part in parsed.path.split("/") if part]
    if len(parts) < 2 or parts[0] != "file" or not parsed.fragment:
        raise ValueError("Expected a MEGA public file URL: https://mega.nz/file/ID#KEY")
    raw_key = b64url_decode(parsed.fragment)
    if len(raw_key) != 32:
        raise ValueError(f"Unexpected MEGA key length: {len(raw_key)} bytes")
    return parts[1], raw_key


def derive_crypto(raw_key: bytes) -> tuple[bytes, bytes]:
    aes_key = bytes(a ^ b for a, b in zip(raw_key[:16], raw_key[16:]))
    iv = raw_key[16:24] + b"\0" * 8
    return aes_key, iv


def api_metadata(handle: str) -> dict:
    api = f"https://g.api.mega.co.nz/cs?id={secrets.randbelow(1_000_000_000)}&n={handle}"
    body = json.dumps([{"a": "g", "g": 1, "p": handle}]).encode("ascii")
    request = urllib.request.Request(api, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=30) as response:
        result = json.load(response)[0]
    if isinstance(result, int):
        raise RuntimeError(f"MEGA API returned error {result}")
    return result


def decrypt_attributes(encoded: str, aes_key: bytes) -> dict:
    encrypted = b64url_decode(encoded)
    decryptor = Cipher(algorithms.AES(aes_key), modes.CBC(b"\0" * 16)).decryptor()
    plain = (decryptor.update(encrypted) + decryptor.finalize()).rstrip(b"\0")
    if not plain.startswith(b"MEGA"):
        raise RuntimeError("MEGA attribute decryption failed")
    return json.loads(plain[4:].decode("utf-8"))


def safe_name(name: str) -> str:
    cleaned = Path(name).name
    if cleaned in {"", ".", ".."}:
        raise ValueError("Unsafe or empty remote filename")
    return cleaned


def download(link: str, output_dir: Path) -> Path:
    handle, raw_key = parse_link(link)
    aes_key, iv = derive_crypto(raw_key)
    metadata = api_metadata(handle)
    attributes = decrypt_attributes(metadata["at"], aes_key)
    filename = safe_name(attributes["n"])
    expected_size = int(metadata["s"])
    output_dir.mkdir(parents=True, exist_ok=True)
    destination = output_dir / filename
    temporary = destination.with_suffix(destination.suffix + ".partial")

    decryptor = Cipher(algorithms.AES(aes_key), modes.CTR(iv)).decryptor()
    request = urllib.request.Request(metadata["g"], headers={"User-Agent": "Mozilla/5.0"})
    written = 0
    with urllib.request.urlopen(request, timeout=60) as response, temporary.open("wb") as stream:
        while True:
            chunk = response.read(4 * 1024 * 1024)
            if not chunk:
                break
            plain = decryptor.update(chunk)
            stream.write(plain)
            written += len(plain)
            print(f"\r{written / 1024 / 1024:.1f} / {expected_size / 1024 / 1024:.1f} MiB", end="", flush=True)
        tail = decryptor.finalize()
        stream.write(tail)
        written += len(tail)
    print()

    if written != expected_size:
        raise RuntimeError(f"Size mismatch: got {written}, expected {expected_size}")
    os.replace(temporary, destination)
    return destination


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("link")
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    try:
        result = download(args.link, args.output_dir)
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(result.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
