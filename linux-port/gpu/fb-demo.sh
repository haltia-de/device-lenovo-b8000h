#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Show the GPU (SGX544) demo on the panel: stop the Phosh desktop (it owns
# the display), unbind fbcon, run gles2_fbdemo on /dev/fb0, then restore
# fbcon and restart the desktop.  Needs Services started (pvrsrvctl).
# Usage: fb-demo.sh [seconds]   (run detached: setsid ... &)
secs=${1:-30}
echo "b8k: fbdemo start ($secs s)" > /dev/kmsg

# The respawn loop first, then the session it started.
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

PVR_FAKE_NODES="pvr_sync" PVR_EXTRA_DEVS="fb0" \
	PVR_LOG_DIR=/root/pvrlog/fbdemo /root/pvr-run.sh /root/gles2_fbdemo "$secs"

set_fbcon 1
/etc/local.d/desktop.start
echo "b8k: fbdemo done" > /dev/kmsg
