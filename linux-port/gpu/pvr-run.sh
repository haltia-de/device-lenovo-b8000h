#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Run a stock Android 4.4 PVR binary (pvrsrvctl, G3 render test) on the
# B8000-H Linux rootfs with the minimum bionic needs:
#  - /dev/__properties__: empty property area (bionic aborts without it);
#  - /dev/log/{main,system,radio,events}: plain files, so liblog output lands
#    in $PVR_LOG_DIR instead of being dropped.  Our /dev/log is the syslogd
#    socket, so the command runs in a private mount namespace with its own
#    small /dev; the host /dev and syslog are left alone.
#
# Usage: pvr-run.sh [--strace] <command> [args...]
#   PVR_LOG_DIR (default /root/pvrlog/<timestamp>) receives log-*.bin,
#   strace.txt (with --strace) and dmesg.txt.  PVR_WITH_FB=1 also exposes
#   /dev/fb0 as /dev/graphics/fb0.
set -eu

use_strace=0
if [ "${1:-}" = "--strace" ]; then
	use_strace=1
	shift
fi
[ $# -gt 0 ] || { echo "usage: $0 [--strace] <command> [args...]" >&2; exit 2; }

log_dir=${PVR_LOG_DIR:-/root/pvrlog/$(date +%Y%m%d-%H%M%S)}
mkdir -p "$log_dir"

# bionic 4.4 prop_area header: magic 'PROP' at +8, version at +12.
# PROP_AREA_VERSION_COMPAT ('vOCE') selects the old TOC layout, which with a
# zero count is a valid empty area.  Lives in devtmpfs: recreate after reboot.
if [ ! -s /dev/__properties__ ]; then
	python3 - <<'EOF'
import struct
area = bytearray(32768)
struct.pack_into('<II', area, 8, 0x504f5250, 0x45434f76)
with open('/dev/__properties__', 'wb') as f:
    f.write(area)
EOF
	chown root:root /dev/__properties__
	chmod 0444 /dev/__properties__
fi

for l in main system radio events; do
	: > "$log_dir/log-$l.bin"
done
dmesg_lines=$(dmesg | wc -l)

export PVR_LOG_DIR="$log_dir" PVR_USE_STRACE="$use_strace"
set +e
unshare -m sh -eu -c '
	mount --make-rprivate /
	d=/run/pvr-dev
	mkdir -p "$d"
	mount -t tmpfs -o mode=0755 pvr-dev "$d"
	for n in null zero full random urandom tty pvrsrvkm __properties__ ion ashmem; do
		if [ -e "/dev/$n" ]; then cp -a "/dev/$n" "$d/"; fi
	done
	mkdir -p "$d/log" "$d/graphics"
	# Stock gralloc may take over the framebuffer; only on request.
	if [ "${PVR_WITH_FB:-0}" = 1 ] && [ -e /dev/fb0 ]; then
		cp -a /dev/fb0 "$d/graphics/fb0"
	fi
	for l in main system radio events; do
		: > "$d/log/$l"
		mount --bind "$PVR_LOG_DIR/log-$l.bin" "$d/log/$l"
	done
	mount --rbind "$d" /dev
	if [ "$PVR_USE_STRACE" = 1 ]; then
		exec strace -f -tt -s 256 -o "$PVR_LOG_DIR/strace.txt" "$@"
	fi
	exec "$@"
' pvr-run "$@"
rc=$?
set -e

# Kernel messages since the start (approximate if the ring buffer wrapped).
dmesg | tail -n +"$((dmesg_lines + 1))" > "$log_dir/dmesg.txt" 2>&1 || true
echo "exit=$rc logs=$log_dir"
exit "$rc"
