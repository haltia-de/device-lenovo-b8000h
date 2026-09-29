#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# One G2+G3 attempt on the tablet: (re)load the GPU modules, start Services
# with the stock pvrsrvctl and run the G3 render test in the same boot.
# Usage: g3-run.sh <tag>   (logs in /root/pvrlog/<tag>, summary on stdout)
# Run detached so a USB console drop cannot cut it:
#   setsid /root/g3-run.sh g3e > /root/pvrlog/g3e-runner.txt 2>&1 < /dev/null &
tag=${1:?tag}
# Modules must match the running kernel: build N keeps them in /root/kN
# (kernel #20 ones are in /root itself).
n=$(uname -r | sed -n 's/.*-b8000h\([0-9]*\)-.*/\1/p')
moddir=/root/k$n
[ -d "$moddir" ] || moddir=/root
log=/root/pvrlog/$tag
echo "b8k: $tag start" > /dev/kmsg

for m in dc_nohw pvrsrvkm; do
	if grep -q "^$m " /proc/modules; then
		rmmod "$m" || { echo "rmmod $m failed"; exit 1; }
	fi
done
if [ -e $moddir/pvrsrvkm.ko.new ]; then
	mv $moddir/pvrsrvkm.ko.new $moddir/pvrsrvkm.ko
fi
grep -q "^ion_legacy " /proc/modules || insmod $moddir/ion_legacy.ko || exit 1
insmod $moddir/pvrsrvkm.ko || exit 1
insmod $moddir/dc_nohw.ko || exit 1
cat /proc/pvr/nodes

# /dev/pvr_sync is not implemented; the kick path handles "no fences".
PVR_FAKE_NODES="pvr_sync" PVR_LOG_DIR="$log" /root/pvr-run.sh --strace \
	/bin/sh -c '/system/vendor/bin/pvrsrvctl --start --no-module;
		echo "pvrsrvctl rc=$?";
		/root/gles2_pbuffer /root/g3.ppm; echo "g3 rc=$?"'
echo "--- UM log"
tail -n 20 "$log/log-main.txt" 2>/dev/null
echo "--- kernel"
tail -n 12 "$log/dmesg.txt"
sync
