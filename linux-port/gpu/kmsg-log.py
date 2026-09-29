#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Append new /dev/kmsg records to a file, fsync'ed per record.

Keeps the kernel log of a GPU test on eMMC even if the tablet hangs right
after an Oops (the ring buffer and pstore may be lost or overwritten).
Usage: kmsg-log.py <output-file>   (run in the background)
"""
import os
import sys

out = os.open(sys.argv[1], os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o644)
kmsg = os.open("/dev/kmsg", os.O_RDONLY)
os.lseek(kmsg, 0, os.SEEK_END)
while True:
    try:
        rec = os.read(kmsg, 8192)
    except BrokenPipeError:
        # Records were overwritten before we read them; carry on.
        continue
    os.write(out, rec)
    os.fsync(out)
