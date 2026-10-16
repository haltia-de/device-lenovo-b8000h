#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Install b8k-weather into a root file system: sh install.sh [ROOT]
# (ROOT empty or / for this system). The tablet shell's build runs it for
# its image; on another system it installs the program the same way.
set -eu
root=${1:-}
root=${root%/}
here=$(cd "$(dirname "$0")" && pwd)
install -D -m 755 "$here/b8k-weather" "$root/usr/local/bin/b8k-weather"
install -D -m 644 "$here/weather.svg" "$root/usr/local/share/b8k/weather.svg"
install -D -m 644 "$here/weather.conf" "$root/usr/local/share/b8k/apps.d/weather.conf"
