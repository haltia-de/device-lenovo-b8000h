#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Install b8k-max into a root file system: sh install.sh [ROOT]
# (ROOT empty or / for this system). The tablet shell's build runs it for
# its image; on another system it installs the program the same way.
set -eu
root=${1:-}
root=${root%/}
here=$(cd "$(dirname "$0")" && pwd)
install -D -m 755 "$here/b8k-max" "$root/usr/local/bin/b8k-max"
install -D -m 644 "$here/max.png" "$root/usr/local/share/b8k/max.png"
install -D -m 644 "$here/max.conf" "$root/usr/local/share/b8k/apps.d/max.conf"
# PyMax (MIT), MAX's web protocol: the pinned wheel from PyPI, checked
# against its sha256 and unpacked next to the program. B8K_CACHE keeps the
# download between builds.
whl=maxapi_python-2.4.1-py3-none-any.whl
cache=${B8K_CACHE:-$here/.cache}
mkdir -p "$cache"
[ -f "$cache/$whl" ] || wget -q -O "$cache/$whl" \
	"https://files.pythonhosted.org/packages/py3/m/maxapi-python/$whl"
echo "49c996cebebdcd490b8fc1424c84faad3c33d0b75eff4bb86cf1de9d968d76ea  $cache/$whl" | sha256sum -c -
rm -rf "$root/usr/local/lib/b8k-max"
mkdir -p "$root/usr/local/lib/b8k-max"
unzip -q -o "$cache/$whl" -d "$root/usr/local/lib/b8k-max"
