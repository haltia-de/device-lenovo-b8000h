#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Enter the Debian armhf (glibc) chroot used for libhybris on the B8000-H.
# The host /dev, /proc, /sys and the stock Android /system (+ /vendor) are
# made visible inside, so bionic blobs load and reach /dev/pvrsrvkm, /dev/ion.
# Usage: debian-enter.sh [command...]   (default: interactive bash)
set -eu
R=${DEBIAN_ROOT:-/opt/debian}

for d in dev proc sys system run tmp; do
	mkdir -p "$R/$d"
done
mountpoint -q "$R/dev" || mount --rbind /dev "$R/dev"
mountpoint -q "$R/proc" || mount -t proc proc "$R/proc"
mountpoint -q "$R/sys" || mount --rbind /sys "$R/sys"
mountpoint -q "$R/system" || mount --bind /system "$R/system"
[ -e "$R/vendor" ] || ln -s /system/vendor "$R/vendor"
cp /etc/resolv.conf "$R/etc/resolv.conf"

# The stock gralloc opens /dev/pvr_sync at init; our KM has no native-sync
# device yet (kicks without fences work), so stand /dev/null in for it.
[ -e /dev/pvr_sync ] || ln -s /dev/null /dev/pvr_sync

[ $# -gt 0 ] || set -- /bin/bash
exec chroot "$R" /usr/bin/env -i HOME=/root TERM="${TERM:-linux}" \
	PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
	LANG=C.UTF-8 "$@"
