#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Zero-copy GPU demo on the panel: stop Phosh (the demo needs DRM master),
# unbind fbcon, run gles2_kmsdemo (SGX renders into gralloc/ION buffers that
# mediatek-drm scans out), then restore fbcon and restart the desktop.
# Needs Services started (pvrsrvctl) and ion_legacy with contiguous buffers.
# Usage: kms-demo.sh [seconds]   (run detached: setsid ... &)
secs=${1:-30}
echo "b8k: kmsdemo start ($secs s)" > /dev/kmsg

pkill -f /etc/local.d/desktop.start
pkill -f /usr/local/bin/b8k-session
pkill phoc
pkill phosh
sleep 2

set_fbcon() {
	for v in /sys/class/vtconsole/vtcon*; do
		grep -q "frame buffer" "$v/name" && echo "$1" > "$v/bind"
	done
}
set_fbcon 0

PVR_FAKE_NODES="pvr_sync" PVR_EXTRA_DEVS="dri" \
	PVR_LOG_DIR=/root/pvrlog/kmsdemo /root/pvr-run.sh /root/gles2_kmsdemo "$secs"

set_fbcon 1
/etc/local.d/desktop.start
echo "b8k: kmsdemo done" > /dev/kmsg
