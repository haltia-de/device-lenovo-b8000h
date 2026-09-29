#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# One G2+G3 attempt on the tablet: (re)load the GPU modules, start Services
# with the stock pvrsrvctl and run the G3 render test in the same boot.
# Usage: g3-run.sh <tag>   (logs in /root/pvrlog/<tag>, summary on stdout)
# Run detached so a USB console drop cannot cut it:
#   setsid /root/g3-run.sh g3e > /root/pvrlog/g3e-runner.txt 2>&1 < /dev/null &
tag=${1:?tag}
log=/root/pvrlog/$tag
echo "b8k: $tag start" > /dev/kmsg

for m in dc_nohw pvrsrvkm; do
	if grep -q "^$m " /proc/modules; then
		rmmod "$m" || { echo "rmmod $m failed"; exit 1; }
	fi
done
if [ -e /root/pvrsrvkm.ko.new ]; then
	mv /root/pvrsrvkm.ko.new /root/pvrsrvkm.ko
fi
grep -q "^ion_legacy " /proc/modules || insmod /root/ion_legacy.ko || exit 1
insmod /root/pvrsrvkm.ko || exit 1
insmod /root/dc_nohw.ko || exit 1
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
