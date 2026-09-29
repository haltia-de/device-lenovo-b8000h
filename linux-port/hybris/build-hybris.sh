#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Build libhybris for the stock Android 4.4.2 (API 19) PowerVR blobs, inside
# the Debian armhf chroot (run through debian-enter.sh).
#   /root/src/libhybris          upstream libhybris (github.com/libhybris)
#   /root/src/android-headers-19 utils/extract-headers.sh on AOSP
#                                android-4.4.2_r1 + vendor kernel-headers
# Installs to /usr/local/hybris.  Log: /root/src/build-hybris.log
set -eu
SRC=/root/src/libhybris/hybris
HDR=/root/src/android-headers-19
PREFIX=/usr/local/hybris

cd "$SRC"
if [ ! -f Makefile ]; then
	NOCONFIGURE=1 ./autogen.sh
	./configure --prefix="$PREFIX" \
		--with-android-headers="$HDR" \
		--enable-arch=arm \
		--enable-property-cache \
		--with-default-hybris-ld-library-path=/vendor/lib:/system/lib \
		--with-default-egl-platform=null
fi
make -j4
make install
ls -la "$PREFIX/lib" | grep -E "libEGL|libGLESv2|libhybris-common|libhardware"
