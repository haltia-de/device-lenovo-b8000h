#!/bin/sh
# Cross-build b8k-press (armv7 musl) inside WSL B8000Build or the
# b8000h-build:3.24 container. Output: /opt/b8000h/tools/b8k-press, which
# build-rootfs.sh installs when it is there.
set -eu
S=/opt/b8000h/sysroot-armv7
if [ ! -e $S/usr/include/linux/input.h ]; then
	mkdir -p $S/etc/apk/keys
	cp /opt/b8000h/rootfs/etc/apk/repositories $S/etc/apk/
	cp /usr/share/apk/keys/armv7/* $S/etc/apk/keys/
	apk --root $S --arch armv7 --initdb --update-cache --no-scripts \
		add musl-dev linux-headers gcc libgcc >/dev/null
fi
G=$(dirname "$(find $S/usr/lib/gcc -name crtbeginS.o | head -1)")
mkdir -p /opt/b8000h/tools
clang --target=armv7-alpine-linux-musleabihf --sysroot=$S -B"$G" -L"$G" \
	-fuse-ld=lld -O2 -Wall -Wextra -o /opt/b8000h/tools/b8k-press \
	"$(dirname "$0")/b8k-press.c"
llvm-strip /opt/b8000h/tools/b8k-press
ls -l /opt/b8000h/tools/b8k-press
