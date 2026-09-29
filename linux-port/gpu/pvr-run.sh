#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Run a stock Android 4.4 PVR binary (pvrsrvctl, G3 render test) on the
# B8000-H Linux rootfs with the minimum bionic needs:
#  - /dev/__properties__: empty property area (bionic aborts without it);
#  - /dev/log/{main,system,radio,events}: FIFOs drained into $PVR_LOG_DIR,
#    so liblog output is kept.  FIFOs rather than plain files: every opener
#    (liblog, the libc abort path) writes from offset 0 without O_APPEND.
#    Our /dev/log is the syslogd socket, so the command runs in a private
#    mount namespace with its own small /dev; host /dev and syslog stay as is.
#
# Usage: pvr-run.sh [--strace] <command> [args...]
#   PVR_LOG_DIR (default /root/pvrlog/<timestamp>) receives log-*.bin (raw),
#   log-{main,system,radio}.txt (decoded), strace.txt (with --strace) and
#   dmesg.txt.  PVR_WITH_FB=1 also exposes /dev/fb0 as /dev/graphics/fb0.
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
# Kernel log start mark: the ring buffer is usually full, so a line count
# does not move; slice by the last timestamp instead.
dmesg_t0=$(dmesg | tail -n 1 | sed -n 's/^\[ *\([0-9.]*\)\].*/\1/p')

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
	readers=
	for l in main system radio events; do
		mkfifo -m 0666 "$d/log/$l"
		# Opened read-write, so the reader never sees EOF between writers.
		( exec 3<>"$d/log/$l"; exec cat <&3 >> "$PVR_LOG_DIR/log-$l.bin" ) &
		readers="$readers $!"
	done
	mount --rbind "$d" /dev
	set +e
	if [ "$PVR_USE_STRACE" = 1 ]; then
		strace -f -tt -s 256 -o "$PVR_LOG_DIR/strace.txt" "$@"
	else
		"$@"
	fi
	rc=$?
	sleep 1
	kill $readers 2>/dev/null
	exit $rc
' pvr-run "$@"
rc=$?
set -e

# liblog entries: priority byte, tag, NUL, message, NUL.
python3 - "$log_dir" <<'EOF'
import os, sys
d = sys.argv[1]
for name in ('main', 'system', 'radio'):
    data = open(os.path.join(d, 'log-%s.bin' % name), 'rb').read()
    out, i = [], 0
    while i < len(data):
        prio = data[i]
        j = data.find(b'\0', i + 1)
        k = data.find(b'\0', j + 1) if j >= 0 else -1
        if j < 0 or k < 0:
            out.append('? %r' % data[i:])
            break
        out.append('%s/%s: %s' % ('??VDIWEFS'[prio] if prio < 9 else prio,
                                  data[i + 1:j].decode(errors='replace'),
                                  data[j + 1:k].decode(errors='replace').rstrip()))
        i = k + 1
    if out:
        with open(os.path.join(d, 'log-%s.txt' % name), 'w') as f:
            f.write('\n'.join(out) + '\n')
EOF

# Kernel messages since the start mark.
dmesg | awk -v t0="${dmesg_t0:-0}" '{ t = $0; sub(/^\[ */, "", t); sub(/\].*/, "", t) }
	t + 0 > t0 + 0' > "$log_dir/dmesg.txt" 2>&1 || true
echo "exit=$rc logs=$log_dir"
exit "$rc"
