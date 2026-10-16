#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Install b8k-paint into a root file system: sh install.sh [ROOT]
# (ROOT empty or / for this system). The tablet shell's build runs it for
# its image; on another system it installs the program the same way.
set -eu
root=${1:-}
root=${root%/}
here=$(cd "$(dirname "$0")" && pwd)
install -D -m 755 "$here/b8k-paint" "$root/usr/local/bin/b8k-paint"
install -D -m 644 "$here/paint.svg" "$root/usr/local/share/b8k/paint.svg"
install -D -m 644 "$here/paint.conf" "$root/usr/local/share/b8k/apps.d/paint.conf"
# The built-in pictures (make-paintings.py makes them).
install -d -m 755 "$root/usr/local/share/b8k/paint"
install -m 644 "$here"/pictures/*.json "$root/usr/local/share/b8k/paint/"
